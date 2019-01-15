#include "Riostream.h"
#include "Ranger.h"

Ranger::Ranger(cTString& rootfile)
  : input_filename(rootfile)
{
  TTree::SetMaxTreeSize(1000000000000);
  changeFile(input_filename);
}

Ranger::~Ranger()
{
  freeFileGracefully(inFile);
  freeFileGracefully(outFile);
}

void Ranger::freeFileGracefully(TFile* fileptr)
{
  if (fileptr != nullptr){
    if(fileptr->IsOpen()){
      fileptr->Close();
    }
    delete fileptr;
  }
}

void Ranger::changeFile(cTString& rootfile)
{
    input_filename = rootfile;

    freeFileGracefully(inFile);

    inFile = TFile::Open(rootfile, "READ");

    // Check whether root file is healthy
    if (!inFile->IsOpen()) {
      std::cerr << "Error: Cannot open file " + input_filename << '\n';
      exit(1);
    }
    if (inFile->IsZombie()) {
      std::cerr << "Error: Root file appears to be damaged. giving up\n";
      exit(1);
    }
}

void Ranger::treeCopy(cTString& tree, cTString& rename)
{
    tree_jobs.push_back({tree,
                         rename == "" ? tree : rename,
                         "", "", "",
                         Action::copytree});
}

void Ranger::treeCopySelection(cTString& treename,
                                   const std::string& branch_selection,
                                   const std::string& cut_selection,
                                   cTString& rename)
{
    tree_jobs.push_back({treename,
                         rename == "" ? treename : rename,
                         branch_selection,
                         "",
                         cut_selection,
                         Action::copytree});
}

void Ranger::flattenTree(cTString& treename,
                             const std::string& branches_flat_selection,
                             const std::string& additional_branches_selection,
                             const std::string& cut_selection,
                             cTString& rename)
{
    tree_jobs.push_back({treename,
                         rename == "" ? treename : rename,
                         branches_flat_selection,
                         additional_branches_selection,
                         cut_selection,
                         Action::flatten_tree});
}

void Ranger::BPVselection(cTString& treename,
                              const std::string& branches_bpv_selection,
                              const std::string& additional_branches_selection,
                              const std::string& cut_selection,
                              cTString& rename)
{
    tree_jobs.push_back({treename,
                         rename == "" ? treename : rename,
                         branches_bpv_selection,
                         additional_branches_selection,
                         cut_selection,
                         Action::bpv_selection});
}

void Ranger::Run(TString output_filename)
{
    // Runs all previously defined jobs in sequence (tree-wise)

    // Create output file
    if(!output_filename.EndsWith(".root")){
        output_filename += ".root";
    }
    freeFileGracefully(outFile);
    outFile = TFile::Open(output_filename, "RECREATE");

    for(const auto& tree_job : tree_jobs){
        switch(tree_job.action){
        case Action::copytree:      SimpleCopy(tree_job);      break;
        case Action::flatten_tree:  flatten(tree_job);         break;
        case Action::bpv_selection: BestPVSelection(tree_job); break;
        default: break;
        }
    }
    outFile->Close();
}

void Ranger::SimpleCopy(const TreeJob& job)
{
    // Copy tree with cut selection and branch selection using built-in methods
    TTree* input_tree = static_cast<TTree*>(inFile->Get(job.name));
    TTree* output_tree = nullptr;
    input_tree->SetBranchStatus("*", 1);

    if(job.cut_selection == ""){
        //output_tree = input_tree->CloneTree(-1, "fast"); // must be tested
        output_tree = input_tree->CloneTree();
    }
    else {
        output_tree = input_tree->CopyTree(TString(job.cut_selection));
    }
    output_tree->SetTitle(job.newname);

    output_tree->Write("", TObject::kOverwrite); // Disable Autosave backups
    delete output_tree;
}

void Ranger::flatten(const TreeJob& job)
{

}

void Ranger::BestPVSelection(const TreeJob& tree_job)
{
    // Loop over tree and copy events into output tree.
    // If TLeaf entries are arrays, select first
    TTree* input_tree = static_cast<TTree*>(inFile->Get(tree_job.name));
    TTree  output_tree(tree_job.newname, tree_job.newname);

    input_tree->SetBranchStatus("*", 0);

    std::cout << "BPV selection on " << tree_job.name << '\n';

    std::vector<TLeaf*> bpv_leaves, all_leaves;

    getListOfBranchesBySelection(all_leaves, input_tree, tree_job.branch_selection);
    getListOfBranchesBySelection(bpv_leaves, input_tree, tree_job.branch_selection2);

    analyzeLeaves_FillLeafBuffers(input_tree, &output_tree, all_leaves, bpv_leaves);
    //analyzeLeaves_FillLeafBuffers(input_tree, &output_tree, additional_leaves);

    int n_entries = input_tree->GetEntriesFast();

    // Event loop
    for(int event = 0; event < n_entries; ++event){
        input_tree->GetEntry(event);
        output_tree.Fill();
    }
    std::cout << "Copy\n";
    TTree* output_tree_selected = output_tree.CopyTree(TString(tree_job.cut_selection));
    output_tree_selected->Write("", TObject::kOverwrite);
}

void Ranger::analyzeLeaves_FillLeafBuffers(TTree* input_tree, TTree* output_tree, std::vector<TLeaf*>& all_leaves, std::vector<TLeaf*>& bpv_leaves)
{
    // Analyzes the selected leaves and finds out their dimensionality
    // Multidimensional leaves are assigned more buffer space according
    // to maximum value in array_length leaf that is returned by leaf->GetLeafCount()

    std::map<TLeaf*, size_t> array_length_leaves; // ... and corresponding buffer sizes

    bool found_const_array = false;

    for (const auto& leaf : all_leaves) {
        std::string leaf_type = leaf->GetTypeName();

        size_t buffer_size = 1;

        // Find out leaf dimension
        Int_t probe;
        TLeaf* dim_leaf = leaf->GetLeafCounter(probe);

        if (dim_leaf == nullptr) {
            if (probe > 1) {
                // Leaf elements are arrays / matrices of constant length > 1
                found_const_array = true;
            }
            // else probe = 1 ->scalar
            buffer_size = probe;
        }
        else {
            // Leaf elements are arrays / matrices of variable length

            // Get max buffer size if unknown
            if(!contains(bpv_leaves, leaf)){
              // Skipping this leaf since its dimension is not aligned with bpv branches
              // Not sure what to do with these. Ignore for now. Maybe write an extra tree for them
              continue;
            }
            if(array_length_leaves.find(dim_leaf) == array_length_leaves.end()){
                input_tree->SetBranchStatus(dim_leaf->GetName(), 1); // !
                array_length_leaves[dim_leaf] = input_tree->GetMaximum(dim_leaf->GetName());
            }
            buffer_size = array_length_leaves[dim_leaf];
        }

        input_tree->SetBranchStatus(leaf->GetName(), 1);

        if(leaf_type == "Float_t"){
            float_leaves.emplace_back(LeafStore<Float_t>(leaf, buffer_size));
            input_tree->SetBranchAddress(leaf->GetName(), &(float_leaves.back().buffer[0]));
            output_tree->Branch(leaf->GetName(), &(float_leaves.back().buffer[0]));
        }
        else if(leaf_type == "Double_t"){
            double_leaves.emplace_back(LeafStore<Double_t>(leaf, buffer_size));
            input_tree->SetBranchAddress(leaf->GetName(), &(double_leaves.back().buffer[0]));
            output_tree->Branch(leaf->GetName(), &(double_leaves.back().buffer[0]));
        }
        else if(leaf_type == "Int_t"){
            int_leaves.emplace_back(LeafStore<Int_t>(leaf, buffer_size));
            input_tree->SetBranchAddress(leaf->GetName(), &(int_leaves.back().buffer[0]));
            output_tree->Branch(leaf->GetName(), &(int_leaves.back().buffer[0]));
        }
    }

    if(array_length_leaves.size() > 1){
        std::cout << "More than one array length leaf found:\n";
        for(auto& arl : array_length_leaves){
            std::cout << arl.first->GetName() << '\n';
        }
        if(found_const_array){
            std::cout << "Alignment leaves and constant array found. Will not select const arrays\n";
        }
        std::cout << "Testing alignment... (not implemented)\n";
    }
}

void Ranger::getListOfBranchesBySelection(std::vector<TLeaf*>& selected, TTree* target_tree, std::string selection)
{
    // Collects leaves that match regex

    TObjArray* leaf_list = target_tree->GetListOfLeaves();

    std::string regex_select;

    // Remove whitespace
    for (auto c = selection.begin(); c != selection.end();) {
        c = (*c == ' ') ? selection.erase(c) : c + 1;
    }
    // Build regex
    if (selection.empty()) {
        return;
    }
    else {
        if (selection.size() >= 2) {
            if(*selection.begin() == '(' && selection.back() == ')') {
                // User entered regex
                regex_select = selection;
            }
        }
        if (regex_select == "" && contains(selection, '*')) {
            // Not a regex. Selected vars by wildcard -> construct regex
            regex_select += "^";
            for(const auto& s : selection){
                regex_select += (s == '*') ? R"([\w\d_]+)" : std::string(1, s);
            }
            regex_select += "$";
        }
        else {
            // Literal variable name -> only one variable can be selected
            regex_select = "^" + selection + "$";
        }
    }
    // Loop over branches, append if regex matches name
    std::regex re(regex_select);

    for (const auto& leaf : *leaf_list) {
        std::smatch match;
        std::string leafName = std::string(leaf->GetName()); // required for std::regex_search
        if (std::regex_match(leafName, re)) {
            selected.push_back(static_cast<TLeaf*>(leaf));
        }
    }
}