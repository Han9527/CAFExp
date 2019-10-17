#include <cmath>
#include <set>
#include <fstream>
#include <algorithm>
#include <iterator>

#include <sys/stat.h>

#include "execute.h"
#include "core.h"
#include "user_data.h"
#include "chisquare.h"
#include "optimizer_scorer.h"
#include "matrix_cache.h"
#include "optimizer.h"
#include "gene_family_reconstructor.h"

double __Qs[] = { 1.000000000190015, 76.18009172947146, -86.50532032941677,
24.01409824083091, -1.231739572450155, 1.208650973866179e-3,
-5.395239384953e-6 };


void estimator::compute(std::vector<model *>& models, const input_parameters &my_input_parameters)
{
    std::ofstream results_file(filename("results", my_input_parameters.output_prefix));

    std::ofstream likelihoods_file(filename("family_lks", my_input_parameters.output_prefix));

    std::vector<double> model_likelihoods(models.size());
    for (size_t i = 0; i < models.size(); ++i) {
        cout << endl << "Inferring processes for " << models[i]->name() << " model" << endl;
        double result = models[i]->infer_family_likelihoods(data.p_prior.get(), data.rootdist, models[i]->get_lambda());
        models[i]->write_vital_statistics(results_file, result);

        models[i]->write_family_likelihoods(likelihoods_file);

        model_likelihoods[i] = result;
    }

    if (model_likelihoods.size() == 2)
    {
        cout << "PValue = " << (1.0 - chi2cdf(2 * (model_likelihoods[1] - model_likelihoods[0]), 1.0));
    }
}

bool compare_result(const optimizer::result& a, const optimizer::result& b)
{
    return a.score < b.score;
}

void estimator::estimate_missing_variables(std::vector<model *>& models, user_data& data)
{
    if (data.p_tree == NULL)
    {
        throw runtime_error("No tree specified for lambda estimation");
    }
    for (model* p_model : models) {
        unique_ptr<inference_optimizer_scorer> scorer(p_model->get_lambda_optimizer(data));
        if (scorer.get() == nullptr)
            continue;   // nothing to be optimized

        optimizer opt(scorer.get());

        if (data.p_error_model)
        {
            // try several different initialization points and optimize them
            vector<optimizer::result> results;
            for (double epsilon = 0.05; epsilon < .5; epsilon += .1)
            {
                data.p_error_model->update_single_epsilon(epsilon);
                results.push_back(opt.optimize(_user_input.optimizer_params));
            }
            auto best = min_element(results.begin(), results.end(), compare_result);
            scorer->finalize(&best->values[0]);
            cout << "Final score: " << best->score << ", Lambda: " << best->values[0] << ", Epsilon: " << best->values[1] * 2 << endl;
        }
        else
        {
            auto result = opt.optimize(_user_input.optimizer_params);
            scorer->finalize(&result.values[0]);
        }
#ifndef SILENT
        p_model->get_monitor().summarize(cerr);
#endif

    }
    if (data.p_lambda == nullptr)
        data.p_lambda = models[0]->get_lambda();

}

void estimator::estimate_lambda_per_family(model *p_model, ostream& ost)
{
    vector<lambda*> result(data.gene_families.size());
    std::transform(data.gene_families.begin(), data.gene_families.end(), result.begin(),
        [this, p_model](gene_family& fam)
    {
#ifndef SILENT
        cout << "Estimating for " << fam.id() << endl;
#endif
        vector<gene_family> v({ fam });
        vector<model *> models{ p_model };
        p_model->set_families(&v);
        data.p_lambda = nullptr;
        estimate_missing_variables(models, data);
        return p_model->get_lambda();
    });
    std::transform(data.gene_families.begin(), data.gene_families.end(), result.begin(),
        ostream_iterator<string>(ost, "\n"),
        [](const gene_family& fam, lambda* lambda)
    {
        return fam.id() + '\t' + lambda->to_string();
    });
    for (auto r : result) delete r; // TODO: use unique_ptrs in result for exception safety

}

/*! Calls estimate_lambda_per_family if the user has set that parameter, otherwise
    calls \ref estimate_missing_variables; \ref compute; \ref compute_pvalues, and \ref model::reconstruct_ancestral_states */
void estimator::execute(std::vector<model *>& models)
{
    string dir = _user_input.output_prefix;
    if (dir.empty()) dir = "results";
    if (mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
    {
        if (errno != EEXIST)
            throw std::runtime_error("Failed to create directory");
    }
    
    if (_user_input.lambda_per_family)
    {
        auto p_model = models[0];   // no support for multiple models
        std::ofstream results_file(filename(p_model->name() + "_lambda_per_family", _user_input.output_prefix));
        estimate_lambda_per_family(p_model, results_file);
    }
    else
    {
        try
        {
            estimate_missing_variables(models, data);

            compute(models, _user_input);

            matrix_cache cache(data.max_family_size + 1);
            for (model* p_model : models) {

                vector<const gene_family*> filtered_families;
                for (auto& gf : data.gene_families)
                {
                    if (p_model->should_calculate_pvalue(gf))
                        filtered_families.push_back(&gf);
                }

                /// For Gamma models, we tried using the most rapidly changing lambda multiplier here, but that
                /// caused issues in the pvalue calculation. It should be best to use the original lambda
                /// instead
                matrix_cache cache(max(data.max_family_size, data.max_root_family_size) + 1);
                cache.precalculate_matrices(get_lambda_values(p_model->get_lambda()), data.p_tree->get_branch_lengths());

                auto pvalues = compute_pvalues(data.p_tree, filtered_families, p_model->get_lambda(), cache, 1000, data.max_family_size, data.max_root_family_size);

                std::unique_ptr<reconstruction> rec(p_model->reconstruct_ancestral_states(filtered_families, &cache, data.p_prior.get()));

                vector<clademap<double>> branch_probabilities(filtered_families.size());

                transform(filtered_families.begin(), filtered_families.end(), branch_probabilities.begin(), [&](const gene_family* gf)
                    {
                        return compute_branch_level_probabilities(data.p_tree, *gf, rec.get(), p_model->get_lambda(), cache, data.max_family_size, data.max_root_family_size);
                    });
                rec->write_results(p_model->name(), _user_input.output_prefix, data.p_tree, filtered_families, pvalues, branch_probabilities);
            }
        }
        catch (const OptimizerInitializationFailure& e )
        {
            initialization_failure_advice(cerr, data.gene_families);
            throw;
        }
    }
}

//Calculate the difference between the Max and Min count for each family, report the 20 families with the largest difference.
void initialization_failure_advice(std::ostream& ost, const std::vector<gene_family>& families)
{
    std::vector<std::pair<std::string, int>> m;
    transform(families.begin(), families.end(), std::inserter(m, m.end()),
        [](const gene_family& gf) { return std::make_pair(gf.id(), gf.species_size_differential()); });
    auto compare = [](const std::pair<string, int>& a, const std::pair<string, int>& b) { return a.second > b.second; };
    sort(m.begin(), m.end(), compare);
    if (m.size() > 20)
        m.resize(20);

    ost << "\nFamilies with largest size differentials:\n";
    for (auto& t : m)
        ost << t.first << ": " << t.second << "\n";
    ost << "\nYou may want to try removing the top few families with the largest difference\nbetween the max and min counts and then re-run the analysis.\n";
}

void chisquare_compare::execute(std::vector<model *>&)
{
    vector<string> chistrings = tokenize_str(_values, ',');
    vector<double> chis(chistrings.size());

    // transform is like R's apply (vector lambdas takes the outputs, here we are making doubles from strings
    transform(chistrings.begin(), chistrings.end(), chis.begin(),
        [](string const& val) { return stod(val); } // this is the equivalent of a Python's lambda function
    );

    double degrees_of_freedom = chis[2];
    cout << "PValue = " << 1.0 - chi2cdf(2 * (chis[1] - chis[0]), degrees_of_freedom) << std::endl;
}

