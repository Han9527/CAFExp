#ifndef root_equilibrium_distribution_h
#define root_equilibrium_distribution_h

#include <vector>

#include "gene_family.h"

class clade;
struct input_parameters;
class root_distribution;

class root_equilibrium_distribution
{
public:
    virtual float compute(size_t val) const = 0;
    virtual void initialize(const root_distribution* root_distribution) = 0;
    virtual ~root_equilibrium_distribution() {}
};

class uniform_distribution : public root_equilibrium_distribution
{
    root_distribution *_p_root_distribution; // in case the user wants to use a specific root size distribution for all simulations
    int _root_distribution_sum = 0;
public:
    uniform_distribution();
    ~uniform_distribution() override;

    virtual void initialize(const root_distribution* root_distribution) override;

    virtual float compute(size_t val) const override;   // creates uniform
};

class poisson_distribution : public root_equilibrium_distribution
{
    std::vector<double> poisson;
    double _poisson_lambda;
public:
    poisson_distribution(std::vector<gene_family> *p_gene_families);

    poisson_distribution(double poisson_lambda) : _poisson_lambda(poisson_lambda)
    {
    }
    virtual void initialize(const root_distribution* root_distribution) override;

    virtual float compute(size_t val) const override
    {
        if (val >= poisson.size())
            return 0;

        return poisson[val];
    }

};

root_equilibrium_distribution* root_eq_dist_factory(const input_parameters& my_input_parameters, std::vector<gene_family> *p_gene_families);
#endif
