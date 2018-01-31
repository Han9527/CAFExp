#include "io.h"
#include "utils.h"
#include <string>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <sstream>
#include <cstring>

using namespace std;

struct option longopts[] = {
  { "infile", required_argument, NULL, 'i' },
  { "error_model", required_argument, NULL, 'e' },
  { "output_prefix", required_argument, NULL, 'o'}, 
  { "tree", required_argument, NULL, 't' },
  { "fixed_lambda", required_argument, NULL, 'l' },
  { "fixed_multiple_lambdas", required_argument, NULL, 'm' },
  { "lambda_tree", required_argument, NULL, 'y' },
  { "n_gamma_cats", required_argument, NULL, 'k' },
  { "fixed_alpha", required_argument, NULL, 'a' },
  { "rootdist", required_argument, NULL, 'f'},
  { "poisson", optional_argument, NULL, 'p' },
  { "simulate", optional_argument, NULL, 's' },
  { "chisquare_compare", required_argument, NULL, 'r' },
  { "log", optional_argument, NULL, 'g'},
  { 0, 0, 0, 0 }
};

void input_parameters::check_input() {
    //! Options -l and -m cannot both specified.
    if (fixed_lambda > 0.0 && !fixed_multiple_lambdas.empty()) {
        throw runtime_error("Options -l and -m are mutually exclusive. Exiting...");
    }

    //! Option -m requires a lambda tree (-y)
    if (!fixed_multiple_lambdas.empty() && lambda_tree_file_path.empty()) {
        throw runtime_error("You must specify a lambda tree (-y) if you fix multiple lambda values (-m). Exiting...");
    }
    
    //! Options -l and -i have to be both specified (if estimating and not simulating).
    if (fixed_lambda > 0.0 && input_file_path.empty() && nsims != 0) {
        throw runtime_error("Options -l and -i must both be provided an argument. Exiting...");
    }
    
    //! The number of simulated families is specified either through -s, or through -f. Cannot be both. 
    if (nsims > 0 && !rootdist.empty()) {
        throw runtime_error("Option -s cannot be provided an argument if -f is specified. Exiting...");
    }
    
    //! Options -i and -f cannot be both specified. Either one or the other is used to specify the root eq freq distr'n.
    if (!input_file_path.empty() && !rootdist.empty()) {
        throw runtime_error("Options -i and -f are mutually exclusive. Exiting...");
    }

    if (fixed_alpha != 0.0 && n_gamma_cats == 1) {
        throw runtime_error("You have to specify both alpha and # of gamma categories to infer parameter values. Exiting...");
    }
}

/* START: Reading in tree data */
//! Read tree from user-provided tree file
/*!
  This function is called by CAFExp's main function when "--tree"/"-t" is specified
*/
clade* read_tree(string tree_file_path, bool lambda_tree) {
    newick_parser parser(false);
    
    // if this function is used to read lambda tree instead of phylogenetic tree
    if (lambda_tree) {
       parser.parse_to_lambdas = true;
    }
    
    ifstream tree_file(tree_file_path.c_str()); // the constructor for ifstream takes const char*, not string, so we need to use c_str()
    if (!tree_file.is_open())
    {
        throw std::runtime_error("Failed to open " + tree_file_path);
    }

    string line;
    
    if (tree_file.good()) {
        getline(tree_file, line);
    }
    tree_file.close();
    
    parser.newick_string = line;
    clade *p_tree = parser.parse_newick();
    
    return p_tree;
}
/* END: Reading in tree data */

/* START: Reading in gene family data */
//! Constructor for simulations (trial is a typedef for a map = {clade *: int}
gene_family::gene_family(trial *t) {
    // for (auto it = t->begin(); it != t->end(); ++it) {
    for (std::map<clade *, int>::iterator it = t->begin(); it != t->end(); ++it) {
	if (it->first->is_leaf()) { this->_species_size_map[it->first->get_taxon_name()] = it->second; }
    }
    
    find_max_size();
}

//! Find and set _max_family_size and _parsed_max_family_size for this family
/*!
  CAFE had a not_root_max (which we use = _parsed_max_family_size; see below) and a root_max = MAX(30, rint(max*1.25));
*/
void gene_family::find_max_size() {
    // Max family size can only be found if there is data inside the object in the first place
    if (!_species_size_map.empty()) {
        _max_family_size = (*max_element(_species_size_map.begin(), _species_size_map.end(), max_value<string, int>)).second;
        _parsed_max_family_size = _max_family_size + std::max(50, _max_family_size/5);
    }
}

//! Mainly for debugging: In case one want to grab the gene count for a given species
int gene_family::get_species_size(std::string species) const {
    // First checks if species data has been entered (i.e., is key in map?)
    if (_species_size_map.find(species) == _species_size_map.end()) {
        throw std::runtime_error(species + " was not found in gene family " + _id);
    }
      
    return _species_size_map.at(species);
}

//! Return first element of pair
template <typename type1, typename type2>
type1 do_get_species(const std::pair<type1, type2> & p1) {
    return p1.first;
}

//! Return vector of species names
vector<std::string> gene_family::get_species() const {
    vector<std::string> species_names(_species_size_map.size());
    transform(_species_size_map.begin(), _species_size_map.end(), species_names.begin(), do_get_species<string,int>); // Transform performs an operation on all elements of a container (here, the operation is the template)
    
    return species_names;
}

//! Read gene family data from user-provided tab-delimited file
/*!
  This function is called by execute::read_gene_family_data, which is itself called by CAFExp's main function when "--infile"/"-i" is specified  
*/
void read_gene_families(std::istream& input_file, clade *p_tree, std::vector<gene_family> *p_gene_families) {
    map<int, std::string> sp_col_map; // For dealing with CAFE input format, {col_idx: sp_name} 
    map<int, string> leaf_indices; // For dealing with CAFExp input format, {idx: sp_name}, idx goes from 0 to number of species
    std::string line;
    bool is_header = true;
    int index = 0;
    
    while (getline(input_file, line)) {
        std::vector<std::string> tokens = tokenize_str(line, '\t');
        // Check if we are done reading the headers
        if (!leaf_indices.empty() && line[0] != '#') { is_header = false; }

        // If still reading header
        if (is_header) {
            
            // Reading header lines, CAFExp input format
            if (line[0] == '#') {
                if (p_tree == NULL) { throw std::runtime_error("No tree was provided."); }
                string taxon_name = line.substr(1); // Grabs from character 1 and on
                clade *p_descendant = p_tree->find_descendant(taxon_name); // Searches (from root down) and grabs clade "taxon_name" root
                
                if (p_descendant == NULL) { throw std::runtime_error(taxon_name + " not located in tree"); }
                if (p_descendant->is_leaf()) { leaf_indices[index] = taxon_name; } // Only leaves matter for computation or estimation
                index++;
            }
            
            // Reading single header line, CAFE input format
            else {
                is_header = false;
                
                // If reading CAFE input format, leaf_indices (which is for CAFExp input format) should be empty
                if (leaf_indices.empty()) {
                    for (int i = 0; i < tokens.size(); ++i) {
                        if (i == 0 || i == 1) {} // Ignoring description and ID columns
                        sp_col_map[i] = tokens[i];
                    }
                }
            }
        }
        
        // Header has ended, reading gene family counts
        else {
            gene_family genfam; 
            
            for (int i = 0; i < tokens.size(); ++i) {                
                // If reading CAFE input format, leaf_indices (which is for CAFExp input format) should be empty
                if (leaf_indices.empty()) {
                    if (i == 0) { genfam.set_desc(tokens[i]); }
                    else if (i == 1) { genfam.set_id(tokens[i]); }
                    else {
                        std::string sp_name = sp_col_map[i];
                        genfam.set_species_size(sp_name, atoi(tokens[i].c_str()));
                        // cout << "Species " << sp_name << " has " << tokens[i] << "gene members." << endl;
                    }
                }
                
                // If reading CAFExp input format
                else {
                    // If index i is in leaf_indices
                    if (leaf_indices.find(i) != leaf_indices.end()) { // This should always be true...
                        std::string sp_name = leaf_indices[i];
                        genfam.set_species_size(sp_name, atoi(tokens[i].c_str()));
                        // cout << "Species " << sp_name << " has " << tokens[i] << "gene members." << endl;
                    }
                    else
                    {
                        if (i == tokens.size() - 1)
                            genfam.set_id(tokens[i]);
                    }
                }
            }
            
            genfam.find_max_size(); // Getting max gene family size for this gene family
            p_gene_families->push_back(genfam);
        }
    }
}
/* END: Reading in gene family data */

/* START: Reading in error model data */
void error_model::set_max_cnt(int max_cnt) {
    _max_cnt = max_cnt;
}

void error_model::set_deviations(std::vector<std::string> deviations) {
    for (std::vector<std::string>::iterator it = deviations.begin(); it != deviations.end(); ++it) {
        _deviations.push_back(std::stoi(*it));
    }
}

void error_model::set_probs(int fam_size, std::vector<double> probs_deviation) {
    if (_error_dists.empty())
        _error_dists.push_back(vector<double>(probs_deviation.size()));

    if (_error_dists.size() <= fam_size)
    {
        _error_dists.resize(fam_size + 1, _error_dists.back());
    }
    _error_dists[fam_size] = probs_deviation; // fam_size starts at 0 at tips, so fam_size = index of vector
}

std::vector<double> error_model::get_probs(int fam_size) const {
    return _error_dists[fam_size];
}

double to_double(string s)
{
    return std::stod(s);
}
//! Read user-provided error model
/*!
  This function is called by execute::read_error_model, which is itself called by CAFExp's main function when "--error_model"/"-e" is specified  
*/
void read_error_model_file(std::istream& error_model_file, error_model *p_error_model) {
    std::string line;
    std::string max_header = "max";
    std::string cnt_diff_header = "cnt";
    
    while (getline(error_model_file, line)) {
        std::vector<std::string> tokens;
        
        // maxcnt line
        if (strncmp(line.c_str(), max_header.c_str(), max_header.size()) == 0) { 
            tokens = tokenize_str(line, ':');
            tokens[1].erase(remove_if(tokens[1].begin(), tokens[1].end(), ::isspace), tokens[1].end()); // removing whitespace
            int max_cnt = std::stoi(tokens[1]);
            p_error_model->set_max_cnt(max_cnt);
        }
        
        // cntdiff line
        else if (strncmp(line.c_str(), cnt_diff_header.c_str(), cnt_diff_header.size()) == 0) { 
            tokens = tokenize_str(line, ' ');
            
            if (tokens.size() % 2 != 0) { 
                throw std::runtime_error("Number of different count differences in the error model (including 0) is not an odd number. Exiting...");
            }
            
            std::vector<std::string> cnt_diffs(tokens.begin()+1, tokens.end());
            
//            cout << "Count diffs are" << endl;
//            for (auto it = cnt_diffs.begin(); it != cnt_diffs.end(); ++it) {
//                cout << *it << " ";
//            }
//            cout << endl;
            p_error_model->set_deviations(cnt_diffs);
        }
        else
        {
            tokens = tokenize_str(line, ' ');
            int sz = std::stoi(tokens[0]);
            vector<double> values(tokens.size()-1);
            transform(tokens.begin() + 1, tokens.end(), values.begin(), to_double);
            p_error_model->set_probs(sz, values);
        }
    }
        // std::vector<std::string> tokens = tokenize_str(line, '\t'); 
}
/* END: Reading in error model data */

//! Populate famdist_map with root family distribution read from famdist_file_path
/*!
  This function is called by CAFExp's main function when "--simulate"/"-s" is specified 
*/
map<int, int>* read_rootdist(string rootdist_file_path) {

    map<int, int> *p_rootdist_map = new map<int, int>();
    ifstream rootdist_file(rootdist_file_path.c_str()); // the constructor for ifstream takes const char*, not string, so we need to use c_str()
    if (!rootdist_file.is_open())
        throw std::runtime_error("Failed to open file '" + rootdist_file_path + "'");
    string line;
    while (getline(rootdist_file, line)) {
        istringstream ist(line);
        int fam_size, fam_count;
        ist >> fam_size >> fam_count;
        (*p_rootdist_map)[fam_size] = fam_count;
    }
  
  return p_rootdist_map;
}

/* START: Printing functions for simulation engine */
//! Print simulations from provided root family distribution
void print_simulation(std::vector<vector<trial *> > &sim, std::ostream& ost) {

    trial::iterator names_it = sim[0][0]->begin();
    
    // printing header with '#' followed by species names, in the order they will appear below
    for (; names_it != sim[0][0]->end(); ++names_it) {
	    ost << "#" << names_it->first->get_taxon_name() << endl;
    }
    
    // printing gene family sizes
    for (int i = 0; i < sim.size(); ++i) { // iterating over gene family sizes     
        for (int j = 0; j < sim[i].size(); ++j) { // iterating over trial of ith gene family size
            for (trial::iterator jth_trial_it = sim[i][j]->begin(); jth_trial_it != sim[i][j]->end(); ++jth_trial_it) { // accessing jth trial inside ith gene family
                ost << jth_trial_it->second << "\t";
            }
            
            ost << endl;
        }
    }
}
/* END: Printing functions for simulation engine */
