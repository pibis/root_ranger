#ifndef LEAFSTORE_H
#define LEAFSTORE_H

#include <vector>
#include <array>
#include <string>

#include "TLeaf.h"

// https://root.cern.ch/doc/v610/classTBranch.html
enum LeafType {
	BYTE_LEAF,
	UBYTE_LEAF,
	SHORT_LEAF,
	USHORT_LEAF,
	INT_LEAF,
	UINT_LEAF,
	FLOAT_LEAF,
	DOUBLE_LEAF,
	LONG_LEAF,
	ULONG_LEAF,
	BOOL_LEAF
};

// All data types supported by root
static const std::array<std::string, 11> DataTypeNames {
	"Char_t",
	"UChar_t",
	"Short_t",
	"UShort_t",
	"Int_t",
	"UInt_t",
	"Float_t",
	"Double_t",
	"Long64_t",
	"ULong64_t",
	"Bool_t"
};
// Datatype postfix
static const std::string DataTypeNamesShort = "BbSsIiFDLlO";

/* LeafStore: Class providing data buffer addresses for the event loop.
*  Since Leaves may contain arrays, the data buffers are stored as a vector
*  max_size=
*/

template <typename T>
class LeafStore {
public:
	LeafStore();
	LeafStore(TLeaf* leaf, LeafType leaf_type, size_t length=1);

	T* operator&();
	TString name(bool appendType=false);

	std::vector<T> buffer; // Temporary buffer containing all array elements

	const TLeaf *leafptr        = nullptr,
	            *dimension_leaf = nullptr;

	const LeafType type = BYTE_LEAF;
};

template<typename T>
LeafStore<T>::LeafStore()
{
	buffer.resize(1);
}

template<typename T>
LeafStore<T>::LeafStore(TLeaf* leaf, LeafType leaf_type, size_t length)
	: leafptr(leaf), type(leaf_type)
{
	dimension_leaf = leafptr->GetLeafCount();

	assert(length >= 1);
	buffer.resize(length);
}

template<typename T>
T* LeafStore<T>::operator&()
{
	// Address of first data entry. (std::vector data is linearly aligned)
	return &data[0];
}

template<typename T>
TString LeafStore<T>::name(bool appendType){
	TString leafname = leafptr->GetName();
	if(dimension_leaf != nullptr){
		leafname += '[';
		leafname += dimension_leaf->GetName();
		leafname += ']';
	}
	if (appendType){
		leafname += '/';
		leafname += DataTypeNamesShort[type];
	}
	return leafname;
}

#endif