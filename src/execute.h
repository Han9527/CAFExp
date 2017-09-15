#ifndef EXECUTE_H
#define EXECUTE_H

#include "io.h"

struct input_parameters;
class lambda;
class probability_calculator;
class core;

class execute {
public:
    //! Read in gene family data (-i)
    std::vector<gene_family> * read_gene_family_data(const input_parameters &my_input_parameters, int &max_family_size, int &max_root_family_size);
    
    //! Read in phylogenetic tree data (-t)
    clade * read_input_tree(const input_parameters &my_input_parameters);

    //! Read in lambda tree (-y)
    clade * read_lambda_tree(const input_parameters &my_input_parameters);

    //! Read in single or multiple lambda (-l or -m)
    lambda * read_lambda(const input_parameters &my_input_parameters, probability_calculator &my_calculator, clade * p_lambda_tree);

    void infer(std::vector<core *>& models, std::vector<gene_family> *p_gene_families, clade *p_tree, lambda *p_lambda, const input_parameters &my_input_parameters, int max_family_size, int max_root_family_size);

    void simulate(std::vector<core *>& models, clade *p_tree, lambda *p_lambda, const input_parameters &my_input_parameters);
};
#endif /* EXECUTE_H */
