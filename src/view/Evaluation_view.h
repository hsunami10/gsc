#pragma once

#include "../model/auth/User.h"

#include <Wt/WContainerWidget>
#include <Wt/Dbo/ptr>

#include <vector>

class Session;
class Submission;
class Eval_item;
class Self_eval;
class Grader_eval;

class File_viewer_widget;

class Evaluation_view : public Wt::WContainerWidget
{
public:
    Evaluation_view(const Wt::Dbo::ptr<Submission>&,
                    Session&,
                    Wt::WContainerWidget* parent = nullptr);

    void go_to(unsigned int);
    void go_default();

private:
    Wt::Dbo::ptr<Submission> submission_;
    Session& session_;
    User::Role role_;

    struct Row_model;

    std::vector<Row_model> model_;
    double total_value_;

    File_viewer_widget* viewer_;
    Wt::WContainerWidget* right_column_;

    class Edit_widget;
    class Response_edit_widget;
    class Boolean_edit_widget;
    class Scale_edit_widget;
    class Informational_edit_widget;

    std::vector<std::unique_ptr<Edit_widget>> rows_;

    // Is the current user allowed to change the self evaluation right now?
    bool can_eval_();

    void load_();
};