#ifndef GAMMA_BUNDLE_H
#define GAMMA_BUNDLE_H

class inference_process;
class gene_family_reconstructor;
class root_equilibrium_distribution;
class matrix_cache;
class lambda;
class clade;
class gene_family;

#include <vector>
#include <iosfwd>
#include <map>

#include "core.h"

class inference_process_factory
{
    std::ostream & _ost;
    lambda* _lambda;
    const clade *_p_tree;
    int _max_family_size;
    int _max_root_family_size;
    std::vector<int> _rootdist_vec; // distribution of relative values. probability can be found by dividing a single value by the total of all values
    int _root_size; // will be drawn from _rootdist_vec by process itself
    const gene_family *_family;
public:

    inference_process_factory(std::ostream & ost, lambda* lambda, const clade *p_tree, int max_family_size,
        int max_root_family_size, std::vector<int> rootdist);

    void set_gene_family(const gene_family *family) {
        _family = family;
    }

    inference_process* operator()(double lambda_multiplier);

    gene_family_reconstructor* create_reconstruction_process(double lambda_multiplier);
};

//! One gamma bundle per family
//! Should reconstruct values for all gamma category probabilities
class gamma_bundle {
    std::vector<inference_process *> _inf_processes;
    std::vector<gene_family_reconstructor *> _rec_processes;

    clademap<double> reconstruction;
    clademap<family_size_change> increase_decrease_map;
    std::vector<double> _category_likelihoods;

public:
    gamma_bundle(inference_process_factory& factory, std::vector<double> lambda_multipliers);
    ~gamma_bundle();

    void clear();

    bool prune(const std::vector<double>& gamma_cat_probs, root_equilibrium_distribution *eq_freq,
        matrix_cache& calc);

    void reconstruct(const std::vector<double>& _gamma_cat_probs);

    double get_lambda_likelihood(int family_id);

    void set_values(matrix_cache *, root_equilibrium_distribution*);

    void print_reconstruction(std::ostream& ost, std::vector<const clade *> order);

    increase_decrease get_increases_decreases(std::vector<const clade *>& order, double pvalue);

    std::vector<const clade *> get_taxa();

    std::vector<double> get_category_likelihoods() const
    {
        return _category_likelihoods;
    }

    std::string get_family_id() const;
};

#endif
