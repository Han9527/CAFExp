#ifndef BASE_MODEL_H
#define BASE_MODEL_H

#include "core.h"

class reconstruction_process;
class matrix_cache;

class base_model : public model {
    std::vector<inference_process *> processes;
    virtual simulation_process* create_simulation_process(int family_number);
public:
    //! Computation or estimation constructor
    base_model(lambda* p_lambda, clade *p_tree, vector<gene_family> *p_gene_families,
        int max_family_size, int max_root_family_size, std::map<int, int> * p_rootdist_map, error_model *p_error_model);

    virtual void start_inference_processes();
    virtual double infer_processes(root_equilibrium_distribution *prior);

    virtual std::string name() {
        return "Base";
    }
    virtual ~base_model();

    virtual void print_results(std::ostream& ost);

    virtual optimizer *get_lambda_optimizer(root_equilibrium_distribution* p_distribution);

    virtual void reconstruct_ancestral_states(matrix_cache *p_calc, root_equilibrium_distribution* p_prior);
    reconstruction_process* create_reconstruction_process(int family_number, matrix_cache *p_calc, root_equilibrium_distribution* p_prior);
    void print_reconstructed_states(std::ostream& ost);

    optimizer *get_epsilon_optimizer(root_equilibrium_distribution* p_distribution);
};

#endif
