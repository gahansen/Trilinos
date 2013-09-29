//@HEADER
// ************************************************************************
//
//   Kokkos: Manycore Performance-Portable Multidimensional Arrays
//              Copyright (2012) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact  H. Carter Edwards (hcedwar@sandia.gov)
//
// ************************************************************************
//@HEADER

#include <gtest/gtest.h>

#include <Kokkos_Serial.hpp>
#include <Kokkos_MemoryTraits.hpp>
#include <Kokkos_CrsMatrix.hpp>

namespace { // anonymous

  using std::cerr;
  using std::endl;

  // Create a square 10 x 10 nonsymmetric diagonally dominant sparse matrix.
  template<typename Device>
  void
  makeSparseMatrix (Kokkos::View<int*, Device>& ptr,
		    Kokkos::View<int*, Device>& ind,
		    Kokkos::View<double*, Device>& val,
		    int& numRows,
		    int& numCols,
		    int& nnz)
  {
    numRows = 10;
    numCols = 10;
    nnz = 21;
    const int ptrRaw[] = {0, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21};
    const int indRaw[] = {0, 1, 9, 
			  1, 2,
			  2, 3,
			  3, 4,
			  4, 5, 
			  5, 6, 
			  6, 7, 
			  7, 8,
			  8, 9,
                       1, 9};
    const double valRaw[] = {1.0, 4.0, 0.5,
			     0.5, 5.0,
			     1.0, 6.0,
			     1.5, 7.0,
			     2.0, 8.0,
			     2.5, 9.0,
			     3.0, 10.0,
			     3.5, 11.0,
			     4.0, 12.0,
			     4.5, 13.0};
    Kokkos::View<const int*, Kokkos::Serial, Kokkos::MemoryUnmanaged> ptrIn (ptrRaw, numRows+1);
    Kokkos::View<const int*, Kokkos::Serial, Kokkos::MemoryUnmanaged> indIn (indRaw, nnz);
    Kokkos::View<const double*, Kokkos::Serial, Kokkos::MemoryUnmanaged> valIn (valRaw, nnz);

    cerr << "Allocating target space" << endl;
    
    ptr = Kokkos::View<int*, Device> ("ptr", numRows + 1);
    ind = Kokkos::View<int*, Device> ("ind", nnz);
    val = Kokkos::View<double*, Device> ("val", nnz);

    cerr << "Copying stuff" << endl;

    Kokkos::deep_copy (ptr, ptrIn);
    Kokkos::deep_copy (ind, indIn);
    Kokkos::deep_copy (val, valIn);

    cerr << "Done copying stuff" << endl;
  }

  // Return the Kokkos::CrsMatrix corresponding to makeSparseMatrix().
  template<typename Device>
  Kokkos::CrsMatrix<double, int, Device>
  makeCrsMatrix ()
  {
    Kokkos::View<int*, Device> ptr;
    Kokkos::View<int*, Device> ind;
    Kokkos::View<double*, Device> val;
    int numRows;
    int numCols;
    int nnz;
    makeSparseMatrix<Device> (ptr, ind, val, numRows, numCols, nnz);

    cerr << "Calling constructor" << endl;

    return Kokkos::CrsMatrix<double, int, Device> ("A", numRows, numCols, nnz, val, ptr, ind);

    cerr << "Done calling constructor" << endl;
  }

  // Create a Kokkos::CrsMatrix.
  // This mainly tests that the class compiles.
  template<typename Device>
  void
  testCrsMatrix ()
  {
    Kokkos::CrsMatrix<double, int, Device> A = makeCrsMatrix<Device> ();
    // mfh 28 Sep 2013: Use A in some way, so the compiler can't
    // optimize it away completely.  This forces the compiler to
    // compile CrsMatrix, which is the whole point of this test.
    printf ("A is %d by %d\n", A.numRows (), A.numCols ());
  }

} // namespace (anonymous)

namespace Test {

class CrsMatrix : public ::testing::Test {
protected:
  static void SetUpTestCase() {}
  static void TearDownTestCase() {}
};

// Just test that CrsMatrix compiles.
TEST_F( CrsMatrix, Compile ) {
  testCrsMatrix<Kokkos::Serial> ();
}

} // namespace test

