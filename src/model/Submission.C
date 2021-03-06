#include "Submission.h"
#include "Self_eval.h"
#include "Assignment.h"
#include "Eval_item.h"
#include "File_data.h"
#include "File_meta.h"
#include "Grader_eval.h"
#include "Session.h"
#include "auth/User.h"

#include <Wt/WDateTime>
#include <Wt/Dbo/Impl>

#include <algorithm>
#include <functional>
#include <locale>
#include <regex>
#include <sstream>

DBO_INSTANTIATE_TEMPLATES(Submission);

Submission::Submission(const dbo::ptr <User>& user,
                       const dbo::ptr <Assignment>& assignment)
        : user1_(user), assignment_(assignment),
          due_date_(assignment->due_date()),
          eval_date_(assignment->eval_date()),
          last_modified_(Wt::WDateTime::currentDateTime())
{

}

static std::regex out_file_re(".*\\.out");

Source_file_vec Submission::source_files_sorted() const
{
    Source_file_vec result;

    for (const auto& ptr : source_files_)
        result.push_back(ptr);

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        bool a_out = std::regex_match(a->name(), out_file_re);
        bool b_out = std::regex_match(b->name(), out_file_re);

        // .out files sort after all other files
        if (a_out && !b_out) return false;
        if (b_out && !a_out) return true;

        return std::lexicographical_compare(
                a->name().begin(), a->name().end(),
                b->name().begin(), b->name().end(),
                boost::is_iless()
        );
    });

    return result;
}

size_t Submission::file_count() const
{
    return source_files().size();
}

bool Submission::extended() const
{
    return due_date_ > assignment_->due_date();
}

bool Submission::eval_extended() const
{
    return eval_date_ > assignment_->eval_date();
}

const Wt::WDateTime& Submission::effective_due_date() const
{
    return std::max(due_date_, assignment_->due_date());
}

const Wt::WDateTime& Submission::effective_eval_date() const
{
    return std::max(eval_date_, assignment_->eval_date());
}

Submission::Status Submission::status() const
{
    auto now = Wt::WDateTime::currentDateTime();

    if (now <= assignment()->open_date())
        return Status::future;
    if (now <= assignment()->due_date())
        return Status::open;
    else if (now <= due_date_)
        return Status::extended;
    else if (now <= assignment()->eval_date())
        return Status::self_eval;
    else if (now <= eval_date_)
        return Status::extended_eval;
    else
        return Status::closed;
}

Submission::Eval_status Submission::eval_status() const
{
    if (is_loaded_) {
        return eval_status_;
    }

    size_t self_evals_size = self_evals_.size();

    if (self_evals_size == assignment()->eval_items().size())
        return Eval_status::complete;
    else if (self_evals_size > 0)
        return Eval_status::started;
    else
        return Eval_status::empty;
}

double Submission::grade() const
{
    if (is_loaded_) {
        return grade_;
    }

    return session()->query<double>(
            "SELECT SUM(relative_value * g.score) / SUM(relative_value)"
            "  FROM eval_items e"
            " INNER JOIN self_evals s ON s.eval_item_id = e.id"
            " INNER JOIN grader_evals g ON g.self_eval_id = s.id"
            " WHERE s.submission_id = ?"
    ).bind(id()).resultValue();
}

const dbo::ptr<Self_eval>&
Submission::get_self_eval(const dbo::ptr<Eval_item>& eval_item,
                          const dbo::ptr<Submission>& submission)
{
    submission->load_cache();

    auto& result = submission->items_[eval_item->sequence()].self_eval;

    if (!result) {
        result = submission.session()->add(new Self_eval(eval_item,
                                                         submission));
        submission.modify()->touch();
    }

    return result;
}

void Submission::retract_self_eval(const dbo::ptr<Self_eval>& self_eval)
{
    auto submission = self_eval->submission();
    auto sequence = self_eval->eval_item()->sequence();

    submission->load_cache();

    submission->items_[sequence].self_eval.remove();
    submission->items_[sequence].self_eval = {};
    submission->items_[sequence].grader_eval = {};

    submission.modify()->touch();
}

void
Submission::save_self_eval(const dbo::ptr<Self_eval>& self_eval,
                           Session& session,
                           double score,
                           const std::string& explanation)
{
    auto self_eval_m = self_eval.modify();
    self_eval_m->set_score(score);
    self_eval_m->set_explanation(explanation);

    auto submission = self_eval->submission();
    auto sequence = self_eval->eval_item()->sequence();

    submission->load_cache();

    if (score == 0.0 &&
            self_eval->eval_item()->type() == Eval_item::Type::Boolean) {
        auto grader_eval = get_grader_eval(self_eval, session.user());
        auto grader_eval_m = grader_eval.modify();
        grader_eval_m->set_score(0.1);
        grader_eval_m->set_explanation("You chose no.");
        grader_eval_m->set_status(Grader_eval::Status::ready);
        submission->items_[sequence].grader_eval = grader_eval;
    } else if (self_eval->eval_item()->type() !=
                   Eval_item::Type::Informational) {
        self_eval->grader_eval().remove();
        submission->items_[sequence].grader_eval = {};
    }

    submission.modify()->touch();
}

const dbo::ptr<Grader_eval>&
Submission::get_grader_eval(const dbo::ptr<Self_eval>& self_eval,
                            const dbo::ptr<User>& grader)
{
    auto submission = self_eval->submission();
    auto sequence = self_eval->eval_item()->sequence();

    submission->load_cache();

    auto& result = submission->items_[sequence].grader_eval;

    if (!result) {
        result = self_eval.session()->add(new Grader_eval(self_eval, grader));
        submission.modify()->touch();
    }

    return result;
}

void Submission::retract_grader_eval(const dbo::ptr<Grader_eval>& grader_eval)
{
    auto submission = grader_eval->submission();
    auto sequence = grader_eval->eval_item()->sequence();

    submission->load_cache();

    submission->items_[sequence].grader_eval.remove();
    submission->items_[sequence].grader_eval = {};

    submission.modify()->touch();
}

void Submission::touch()
{
    last_modified_ = Wt::WDateTime::currentDateTime();
}

dbo::ptr<Submission>
Submission::find_by_assignment_and_user(dbo::Session& session,
                                        const dbo::ptr<Assignment>& assignment,
                                        const dbo::ptr<User>& user)
{
    dbo::ptr<Submission> result =
            session.find<Submission>()
                   .where("user1_id = ? OR user2_id = ?")
                   .bind(user.id()).bind(user.id())
                   .where("assignment_number = ?")
                   .bind(assignment->number());
    if (!result)
        result = session.add(new Submission(user, assignment));

    return result;
}

bool Submission::can_view(const dbo::ptr<User>& user) const
{
    return user->can_admin() || user == user1_ || user == user2_;
}

bool Submission::can_submit(const dbo::ptr<User>& user) const
{
    auto now = Wt::WDateTime::currentDateTime();

    return user->can_admin() ||
            (now <= effective_due_date() &&
                    (user == user1_ || user == user2_));
}

bool Submission::can_eval(const dbo::ptr<User>& user) const
{
    auto now = Wt::WDateTime::currentDateTime();

    return user->can_admin() ||
           (effective_due_date() < now &&
                   now <= effective_eval_date() &&
                   (user == user1_ || user == user2_));
}

bool Submission::can_view_eval(const dbo::ptr<User>& user) const
{
    auto now = Wt::WDateTime::currentDateTime();

    return user->can_admin() ||
           (effective_due_date() < now &&
            (user == user1_ || user == user2_));
}

std::string Submission::url() const
{
    std::ostringstream result;

    result << "/~" << user1_->name();
    result << "/hw/" << assignment_->number();

    return result.str();
}

std::string Submission::eval_url() const
{
    return url() + "/eval";
}

size_t Submission::item_count() const
{
    if (is_loaded_) return item_count_;
    return assignment()->eval_items().size();
}

const std::vector<Submission::Item>& Submission::items() const
{
    load_cache();
    return items_;
}

double Submission::point_value() const
{
    if (is_loaded_) return point_value_;

    return session()->query<double>(
            "SELECT SUM(relative_value)"
            "  FROM eval_items"
            " WHERE assignment_number = ?"
    ).bind(assignment()->number()).resultValue();
}

bool Submission::is_evaluated() const
{
    return eval_status() == Eval_status::complete;
}

bool Submission::is_graded() const
{
    if (is_loaded_) {
        return is_graded_;
    }

    auto grader_eval_count = session()->query<int>(
            "SELECT COUNT(*)"
            "  FROM self_evals s"
            " INNER JOIN grader_evals g ON g.self_eval_id = s.id"
            " WHERE s.submission_id = ?"
            "   AND g.status = ?"
    ).bind(id()).bind((int) Grader_eval::Status::ready).resultValue();

    return grader_eval_count == self_evals_.size();
}

void Submission::load_cache() const
{
    if (is_loaded_) return;

    item_count_ = 0;
    grade_ = 0;
    point_value_ = 0;

    for (const auto& eval_item : assignment()->eval_items()) {
        ++item_count_;
        point_value_ += eval_item->relative_value();
        auto sequence = eval_item->sequence();
        while (items_.size() <= sequence) items_.emplace_back();
        items_[sequence].eval_item = eval_item;
    }

    size_t self_eval_count = 0;
    size_t grader_eval_count = 0;

    for (const auto& self_eval : self_evals_) {
        ++self_eval_count;

        auto sequence = self_eval->eval_item()->sequence();
        items_[sequence].self_eval = self_eval;

        auto grader_eval = self_eval->grader_eval();
        if (grader_eval) {
            items_[sequence].grader_eval = grader_eval;
            if (grader_eval->status() == Grader_eval::Status::ready) {
                ++grader_eval_count;
                grade_ += grader_eval->score() *
                          self_eval->eval_item()->relative_value();
            }
        }
    }

    eval_status_ = self_eval_count == item_count_ ? Eval_status::complete
                 : self_eval_count == 0           ? Eval_status::empty
                 /* otherwise */                  : Eval_status::started;

    is_graded_ = grader_eval_count == item_count_;

    grade_ /= point_value_;

    is_loaded_ = true;
}

std::string Submission::owner_string() const
{
    std::string result = user1()->name();
    if (user2()) {
        result += '+';
        result += user2()->name();
    }

    return result;
}

std::string Submission::grade_string() const
{
    return Eval_item::pct_string(grade(), 3);
}

