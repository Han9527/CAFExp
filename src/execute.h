#ifndef EXECUTE_H
#define EXECUTE_H

#include <vector>

#include "io.h"

struct input_parameters;
class lambda;
class matrix_cache;
class model;
class root_equilibrium_distribution;
class user_data;

typedef clademap<int> trial;

class action
{
protected:
    user_data& data;
    const input_parameters &_user_input;
public:
    virtual void execute(std::vector<model *>& models) = 0;
    action(user_data& d, const input_parameters& ui) : data(d), _user_input(ui)
    {

    }
    virtual ~action() {}
};

class simulator : public action
{
    void simulate(std::vector<model *>& models, const input_parameters &my_input_parameters);
public:
    simulator(user_data& d, const input_parameters& ui) : action(d, ui)
    {

    }
    virtual void execute(std::vector<model *>& models);
    void print_simulations(std::ostream& ost, bool include_internal_nodes, const std::vector<trial *>& results);
    void simulate_processes(model *p_model, std::vector<trial *>& results);

};

class chisquare_compare : public action
{
    std::string _values;
public:
    chisquare_compare(user_data& d, const input_parameters& ui) : action(d, ui)
    {
        _values = ui.chisquare_compare;
    }
    virtual void execute(std::vector<model *>& models);
};

class estimator : public action
{
    vector<double> compute_pvalues(const user_data& data, int number_of_simulations);
public:
    estimator(user_data& d, const input_parameters& ui) : action(d, ui)
    {

    }

    virtual void execute(std::vector<model *>& models);

    void compute(std::vector<model *>& models, const input_parameters &my_input_parameters, int max_family_size, int max_root_family_size);

    void estimate_missing_variables(std::vector<model *>& models, user_data& data);
};

action* get_executor(input_parameters& user_input, user_data& data);

#endif /* EXECUTE_H */
