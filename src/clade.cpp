#include "clade.h"
#include "gene_family.h"
#include "utils.h"
#include "io.h"

using namespace std;

/* Recursive destructor */
clade::~clade() {

  for (auto i : _descendants) {
    delete i; // recursively delete all descendants
  }
}

clade *clade::get_parent() const {

  return _p_parent; // memory address
}

double clade::get_branch_length() const
{
    if (is_lambda_clade)
        throw std::runtime_error("Requested branch length from lambda tree");

    return _branch_length;
}

int clade::get_lambda_index() const
{
    if (!is_lambda_clade)
        throw std::runtime_error("Requested lambda index from branch length tree");

    return _lambda_index;
}

/* Adds descendant to vector of descendants */
void clade::add_descendant(clade *p_descendant) {
	
  _descendants.push_back(p_descendant);
  _name_interior_clade();
  if (!is_root()) {
    _p_parent->_name_interior_clade();
  }
}

/* Recursively fills vector of names provided as argument */
void clade::add_leaf_names(vector<string> &vector_names) {

    if (_descendants.empty()) {
        vector_names.push_back(_taxon_name); // base case (leaf), and it starts returning
    }
    else {
        for (auto desc : _descendants) {
            desc->add_leaf_names(vector_names);
        }
    }
}

/* Recursively finds internal nodes, and returns vector of clades */
vector<const clade*> clade::find_internal_nodes() const {

  vector<const clade*> internal_nodes;

  /* Base case: returns empty vector */
  if (is_leaf()) { return internal_nodes; }

  else {
    internal_nodes.push_back(this);

    for (auto i : _descendants) {
      auto descendant = i->find_internal_nodes(); // recursion
      if (!descendant.empty()) { internal_nodes.insert(internal_nodes.end(), descendant.begin(), descendant.end()); }
    }

    return internal_nodes;
  }
}


  /* Recursively find pointer to clade with provided taxon name */
const clade *clade::find_descendant(string some_taxon_name) const {

    const clade *p_descendant;
    auto descendant_finder = [some_taxon_name, &p_descendant](const clade *clade) {
        if (clade->get_taxon_name() == some_taxon_name)
            p_descendant = clade;
    };

  apply_prefix_order(descendant_finder);

  return p_descendant;
}
  
/* Finds branch length of clade with provided taxon name. Does so by calling find_descendant(), which recursively searches the tree */
double clade::find_branch_length(string some_taxon_name) {

  auto clade = find_descendant(some_taxon_name);
  if (clade == NULL || clade->is_root()) { return 0; } // guarding against root query

  cout << "Found matching clade" << endl;
  return clade->_branch_length;
}

/* Names interior clades, starting from clade of first method call and going up the tree until root */
void clade::_name_interior_clade() {

    vector<string> descendant_names; // vector of names
    add_leaf_names(descendant_names); // fills vector of names
    sort(descendant_names.begin(), descendant_names.end()); // sorts alphabetically (from std)
    _taxon_name.clear(); // resets whatever taxon_name was
    for (auto name : descendant_names) {
        _taxon_name += name;
    }

    if (_p_parent)
        _p_parent->_name_interior_clade();
}

bool clade::is_leaf() const {

  return _descendants.empty();
}

bool clade::is_root() const {

  return get_parent() == NULL;
}

/* Function print_clade_name() is used in conjunction with apply_reverse_level_order and apply_prefix order for debugging purposes.
   e.g.,
   cout << "Tree " << p_tree->get_taxon_name() << " in reverse order: " << endl;
   p_tree->apply_reverse_level_order(print_name)   
*/
void print_clade_name(clade *clade) {
  cout << clade->get_taxon_name() << " (length of subtending branch: " << clade->get_branch_length() << ")" << endl;
}

std::map<std::string, int> clade::get_lambda_index_map()
{
    std::map<std::string, int> node_name_to_lambda_index;
    
    auto fn = [&node_name_to_lambda_index](const clade *c) { 
        node_name_to_lambda_index[c->get_taxon_name()] = c->get_lambda_index() - 1; // -1 is to adjust the index offset
    };

    apply_prefix_order(fn);
	return node_name_to_lambda_index;
}

void clade::write_newick(ostream& ost, std::function<std::string(const clade *c)> textwriter) const
{
    if (is_leaf()) {
        ost << textwriter(this);
    }
    else {
        ost << '(';

        // some nonsense to supress trailing comma
        for (size_t i = 0; i< _descendants.size() - 1; i++) {
            _descendants[i]->write_newick(ost, textwriter);
            ost << ',';
        }

        _descendants[_descendants.size() - 1]->write_newick(ost, textwriter);
        ost << ')' << textwriter(this);
    }
}

string clade_index_or_name(const clade* node, const cladevector& order)
{
    if (node->is_leaf())
        return node->get_taxon_name();
    else
    {
        return to_string(distance(order.begin(), find(order.begin(), order.end(), node)));
    }
}

std::set<double> clade::get_branch_lengths() const
{
    set<double> result;
    auto branch_length_func = [&result](const clade* c) { 
        if (c->get_branch_length() > 0.0)
            result.insert(c->get_branch_length()); 
    };
    apply_prefix_order(branch_length_func);
    return result;
}
