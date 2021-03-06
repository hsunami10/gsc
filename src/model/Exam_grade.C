#include "Exam_grade.h"
#include "Eval_item.h"
#include "auth/User.h"

DBO_INSTANTIATE_TEMPLATES(Exam_grade);

Exam_grade::Exam_grade(const dbo::ptr <User>& user, int number)
        : user_(user),
          number_(number),
          points_(0),
          possible_(0)
{ }

void Exam_grade::set_points_and_possible(int points, int possible)
{
    points_ = points;
    possible_ = possible;
}

std::string Exam_grade::pct_string() const
{
    if (possible() == 0) return "N/A";
    return Eval_item::pct_string(double(points()) / possible(), 3);
}

Wt::Dbo::collection<Wt::Dbo::ptr<Exam_grade>>
Exam_grade::find_by_user(const dbo::ptr<User>& user)
{
    return user.session()->find<Exam_grade>()
               .where("user_id = ?").bind(user.id())
               .orderBy("number");
}

dbo::ptr<Exam_grade>
Exam_grade::get_by_user_and_number(const dbo::ptr<User>& user, int number)
{
    dbo::ptr<Exam_grade> result =
            user.session()->find<Exam_grade>()
                .where("user_id = ?").bind(user.id())
                .where("number = ?").bind(number);

    if (! result)
        return user.session()->add(new Exam_grade(user, number));
    else
        return result;
}

