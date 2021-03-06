/*
// @HEADER
// ***********************************************************************
//
//          Tpetra: Templated Linear Algebra Services Package
//                 Copyright (2008) Sandia Corporation
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
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ************************************************************************
// @HEADER
*/

#include <Tpetra_ConfigDefs.hpp>
#include "TpetraExt_MatrixMatrix.hpp"
#include "Tpetra_MatrixIO.hpp"
#include "Tpetra_DefaultPlatform.hpp"
#include "Tpetra_Vector.hpp"
#include "Tpetra_CrsMatrixMultiplyOp.hpp"
#include "Tpetra_Import.hpp"
#include "Tpetra_Export.hpp"
#include "Teuchos_DefaultComm.hpp"
#include "Teuchos_CommandLineProcessor.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_UnitTestHarness.hpp"
#include "MatrixMarket_Tpetra.hpp"
#include "Tpetra_RowMatrixTransposer.hpp"
#include <cmath>

namespace {
  static const double defaultEpsilon = 1e-10;
  bool verbose = false;
  std::string matnamesFile;

  using Teuchos::null;
  using Teuchos::rcp;
  using Teuchos::RCP;
  using Teuchos::tuple;
  using Tpetra::global_size_t;
  using Teuchos::Comm;
  using Tpetra::CrsMatrix;
  using Tpetra::Map;
  using Teuchos::Array;
  using Tpetra::Vector;
  using Tpetra::CrsMatrixMultiplyOp;
  using Tpetra::DefaultPlatform;
  using Tpetra::MatrixMarket::Reader;
  using Teuchos::ArrayView;
  using Teuchos::FancyOStream;
  using Teuchos::ParameterList;
  using Tpetra::RowMatrixTransposer;
  typedef Tpetra::Details::DefaultTypes::node_type node_type;

  typedef CrsMatrix<double, int, int, node_type> Matrix_t;


TEUCHOS_STATIC_SETUP(){
  Teuchos::CommandLineProcessor &clp = Teuchos::UnitTestRepository::getCLP();
  clp.setOption("matnames-file", &matnamesFile,
    "A file containing a list of matricies we'll import", true);
  clp.setOption("v", "not-verbose", &verbose,
    "Whether or not to use verbose output");
}

template<
  class Scalar,
  class LocalOrdinal,
  class GlobalOrdinal,
  class Ordinal>
RCP<CrsMatrix<Scalar, LocalOrdinal, GlobalOrdinal, node_type> >
getIdentityMatrix(
  global_size_t globalNumRows,
  RCP<const Comm<Ordinal> > comm,
  RCP<node_type> node)
{
  RCP<const Map<LocalOrdinal,GlobalOrdinal,node_type> > identityRowMap =
    Tpetra::createUniformContigMapWithNode<LocalOrdinal,GlobalOrdinal,node_type>(
      globalNumRows, comm, node);
  RCP<CrsMatrix<Scalar, LocalOrdinal,GlobalOrdinal, node_type> > identityMatrix =
    Tpetra::createCrsMatrix<Scalar, LocalOrdinal, GlobalOrdinal, node_type>(identityRowMap, 1);
  for(
    ArrayView<const int>::iterator it =
      identityRowMap->getNodeElementList().begin();
    it != identityRowMap->getNodeElementList().end();
    ++it)
  {
    Array<GlobalOrdinal> col(1,*it);
    Array<Scalar> val(1,Teuchos::ScalarTraits<Scalar>::one());
    identityMatrix->insertGlobalValues(*it, col(), val());
  }
  identityMatrix->fillComplete();
  return identityMatrix;
}




typedef struct add_test_results_struct{
  double correctNorm;
  double computedNorm;
  double epsilon;
} add_test_results;

typedef struct mult_test_results_struct{
  double epsilon;
  double cNorm;
  double compNorm;
} mult_test_results;


template<class Ordinal>
add_test_results regular_add_test(
    RCP<Matrix_t > A,
    RCP<Matrix_t > B,
    bool AT,
    bool BT,
    RCP<Matrix_t > C,
    RCP<const Comm<Ordinal> > comm)
{
  add_test_results toReturn;
  toReturn.correctNorm = C->getFrobeniusNorm ();

  RCP<const Map<int,int, node_type> > rowmap = AT ? A->getDomainMap() : A->getRowMap();
  RCP<Matrix_t> computedC = rcp( new Matrix_t(rowmap, 1));

  Tpetra::MatrixMatrix::Add(*A, AT, 1.0, *B, BT, 1.0, computedC);

  computedC->fillComplete(A->getDomainMap(), A->getRangeMap());
  toReturn.computedNorm = computedC->getFrobeniusNorm ();
  toReturn.epsilon = fabs(toReturn.correctNorm - toReturn.computedNorm);

  return toReturn;
}

/// \brief Test the three-argument (A, B, C) version of CrsMatrix add,
///   where the output argument C is null on input.
///
/// \tparam CrsMatrixType A specialization of Tpetra::CrsMatrix.
template<class CrsMatrixType>
add_test_results
null_add_test (const CrsMatrixType& A,
               const CrsMatrixType& B,
               const bool AT,
               const bool BT,
               const CrsMatrixType& C,
               Teuchos::FancyOStream& out)
{
  using Teuchos::RCP;
  using std::endl;
  typedef typename CrsMatrixType::scalar_type scalar_type;
  typedef typename CrsMatrixType::local_ordinal_type local_ordinal_type;
  typedef typename CrsMatrixType::global_ordinal_type global_ordinal_type;
  typedef typename CrsMatrixType::node_type NT;
  typedef Teuchos::ScalarTraits<scalar_type> STS;
  typedef Tpetra::Map<local_ordinal_type, global_ordinal_type, NT> map_type;
  typedef Tpetra::Export<local_ordinal_type, global_ordinal_type, NT> export_type;
  const scalar_type one = STS::one ();

  out << "  Computing Frobenius norm of the expected result C" << endl;
  add_test_results toReturn;
  toReturn.correctNorm = C.getFrobeniusNorm ();

  out << "  Calling 3-arg add" << endl;
  RCP<const map_type> domainMap = BT ? B.getRangeMap () : B.getDomainMap ();
  RCP<const map_type> rangeMap = BT ? B.getDomainMap () : B.getRangeMap ();
  RCP<CrsMatrixType> C_computed =
    Tpetra::MatrixMatrix::add (one, AT, A, one, BT, B,
                               domainMap, rangeMap, Teuchos::null);
  TEUCHOS_TEST_FOR_EXCEPTION(
    C_computed.is_null (), std::logic_error, "3-arg add returned null.");

  RCP<CrsMatrixType> C_exported;
  if (! C_computed->getRowMap ()->isSameAs (* (C.getRowMap ()))) {
    // Export C_computed to C's row Map, so we can compare the two.
    export_type exp (C_computed->getRowMap (), C.getRowMap ());
    C_exported =
      Tpetra::exportAndFillCompleteCrsMatrix<CrsMatrixType> (C_computed, exp,
                                                             C.getDomainMap (),
                                                             C.getRangeMap ());
  } else {
    C_exported = C_computed;
  }

  TEUCHOS_TEST_FOR_EXCEPTION(
    ! C_exported->getRowMap ()->isSameAs (* (C.getRowMap ())),
    std::logic_error,
    "Sorry, C_exported and C have different row Maps.");
  TEUCHOS_TEST_FOR_EXCEPTION(
    ! C_exported->getDomainMap ()->isSameAs (* (C.getDomainMap ())),
    std::logic_error,
    "Sorry, C_exported and C have different domain Maps.");
  TEUCHOS_TEST_FOR_EXCEPTION(
    ! C_exported->getRangeMap ()->isSameAs (* (C.getRangeMap ())),
    std::logic_error,
    "Sorry, C_exported and C have different range Maps.");

  toReturn.computedNorm = C_exported->getFrobeniusNorm ();
  toReturn.epsilon = STS::magnitude (toReturn.correctNorm - toReturn.computedNorm);
  return toReturn;
}


template<class Ordinal>
add_test_results add_into_test(
    RCP<Matrix_t > A,
    RCP<Matrix_t > B,
    bool AT,
    RCP<Matrix_t > C,
    RCP<const Comm<Ordinal> > comm)
{
  add_test_results toReturn;
  toReturn.correctNorm = C->getFrobeniusNorm ();

  RCP<const Map<int, int, node_type> > rowmap =
    AT ? A->getDomainMap () : A->getRowMap ();
  RCP<Matrix_t> computedC = rcp (new Matrix_t (rowmap, 1));
  Tpetra::MatrixMatrix::Add (*A, AT, 1.0, *B, 1.0);
  B->fillComplete ();
  toReturn.computedNorm = B->getFrobeniusNorm ();
  toReturn.epsilon = fabs (toReturn.correctNorm - toReturn.computedNorm);

  return toReturn;
}

template<class Ordinal>
mult_test_results multiply_test(
  const std::string& name,
  RCP<Matrix_t> A,
  RCP<Matrix_t> B,
  bool AT,
  bool BT,
  RCP<Matrix_t> C,
  RCP<const Comm<Ordinal> > comm,
  FancyOStream& out)
{
  typedef Map<int, int, node_type> Map_t;
  RCP<const Map_t> map = C->getRowMap();

  RCP<Matrix_t> computedC = rcp( new Matrix_t(map, 1));

  Tpetra::MatrixMatrix::Multiply(*A, AT, *B, BT, *computedC, false);
  computedC->fillComplete(C->getDomainMap(), C->getRangeMap());

#if 0
  Tpetra::MatrixMarket::Writer<Matrix_t>::writeSparseFile(
    name+"_calculated.mtx", computedC);
  Tpetra::MatrixMarket::Writer<Matrix_t>::writeSparseFile(
    name+"_real.mtx", C);
#endif

  RCP<Matrix_t> diffMatrix = Tpetra::createCrsMatrix<double, int, int, node_type>(C->getRowMap());
  Tpetra::MatrixMatrix::Add(*C, false, -1.0, *computedC, false, 1.0, diffMatrix);
  diffMatrix->fillComplete(C->getDomainMap(), C->getRangeMap());

  mult_test_results results;
  results.cNorm    = C->getFrobeniusNorm ();
  results.compNorm = diffMatrix->getFrobeniusNorm ();
  results.epsilon  = results.compNorm/results.cNorm;

  return results;
}


template<class Ordinal>
mult_test_results multiply_reuse_test(
  const std::string& name,
  RCP<Matrix_t> A,
  RCP<Matrix_t> B,
  bool AT,
  bool BT,
  RCP<Matrix_t> C,
  RCP<const Comm<Ordinal> > comm,
  FancyOStream& out)
{
  typedef Map<int, int, node_type> Map_t;
  typedef Vector<double,int,int,node_type> Vector_t;

  RCP<const Map_t> map = C->getRowMap();

  // Scaling vectors
  Teuchos::Array<typename Teuchos::ScalarTraits<double>::magnitudeType> norms(1);

  RCP<Vector_t> leftScaling  = rcp( new Vector_t(C->getRangeMap()) );
  leftScaling->randomize();
  leftScaling->norm2(norms);
  leftScaling->scale(1.0/norms[0]);

  RCP<Vector_t> rightScaling = rcp( new Vector_t(C->getDomainMap()) );
  rightScaling->randomize();
  rightScaling->norm2(norms);
  rightScaling->scale(1.0/norms[0]);

  // computedC1 = leftScaling * (op(A)*op(B)) * rightScaling
  RCP<Matrix_t> computedC1 = rcp( new Matrix_t(map, 1));
  Tpetra::MatrixMatrix::Multiply(*A, AT, *B, BT, *computedC1, false/*call_FillCompleteOnResult*/);
  computedC1->fillComplete(C->getDomainMap(), C->getRangeMap());
  computedC1->leftScale (*leftScaling);
  computedC1->rightScale(*rightScaling);

  RCP<node_type> node = KokkosClassic::Details::getNode<node_type>();

  // As = leftScaling * op(A) =
  //   leftScaling * A, if AT=false
  //   A*leftScaling,   if AT=true
  RCP<Matrix_t> As = A->clone(node);
  if (AT == false) As->leftScale (*leftScaling);
  else             As->rightScale(*leftScaling);

  // Bs = op(B) * rightScaling =
  //   B * rightScaling, if BT=false
  //   rightScaling*B,   if BT=true
  RCP<Matrix_t> Bs = B->clone(node);
  if (BT == false) Bs->rightScale(*rightScaling);
  else             Bs->leftScale (*rightScaling);

  // computedC2 = op(As) * op(Bs)
  RCP<Matrix_t> computedC2 = rcp( new Matrix_t(C->getCrsGraph()) );
  computedC2->fillComplete(C->getDomainMap(), C->getRangeMap());

  computedC2->resumeFill();
  Tpetra::MatrixMatrix::Multiply(*As, AT, *Bs, BT, *computedC2, true/*call_FillCompleteOnResult*/);

  // diffMatrix = computedC2 - computedC1
  RCP<Matrix_t> diffMatrix = Tpetra::createCrsMatrix<double, int, int, node_type>(C->getRowMap());
  Tpetra::MatrixMatrix::Add(*computedC1, false, -1.0, *computedC2, false, 1.0, diffMatrix);
  diffMatrix->fillComplete(C->getDomainMap(), C->getRangeMap());

  mult_test_results results;
  results.cNorm    = C->getFrobeniusNorm ();
  results.compNorm = diffMatrix->getFrobeniusNorm ();
  results.epsilon  = results.compNorm/results.cNorm;

  return results;
}


mult_test_results jacobi_test(
  const std::string& name,
  RCP<Matrix_t> A,
  RCP<Matrix_t> B,
  RCP<const Comm<int> > comm,
  FancyOStream& out)
{
  typedef Vector<double,int,int,node_type> Vector_t;
  typedef Map<int, int, node_type> Map_t;
  RCP<const Map_t> map = A->getRowMap();

  double omega=1.0;
  Vector_t Dinv(B->getRowMap());
  Dinv.putScalar(1.0);

  // Jacobi version
  RCP<Matrix_t> C = rcp(new Matrix_t(B->getRowMap(),0));
  Tpetra::MatrixMatrix::Jacobi(omega,Dinv,*A,*B,*C);

  // Multiply + Add version
  Dinv.putScalar(omega);
  RCP<Matrix_t> AB = rcp(new Matrix_t(B->getRowMap(),0));
  RCP<Matrix_t> C_check = rcp(new Matrix_t(B->getRowMap(),0));
  Tpetra::MatrixMatrix::Multiply(*A,false,*B,false,*AB);
  AB->leftScale(Dinv);
  Tpetra::MatrixMatrix::Add(*AB,false,-1.0,*B,false,1.0,C_check);

  // Check the difference
  Tpetra::MatrixMatrix::Add(*C, false, -1.0, *C_check, 1.0);
  C_check->fillComplete(B->getDomainMap(),B->getRangeMap());

  // Error Check
  double compNorm = C_check->getFrobeniusNorm();
  double cNorm = C->getFrobeniusNorm();
  mult_test_results results;
  results.epsilon  = compNorm/cNorm;
  results.cNorm    = cNorm;
  results.compNorm = compNorm;
  return results;
}


mult_test_results jacobi_reuse_test(
  const std::string& name,
  RCP<Matrix_t> A,
  RCP<Matrix_t> B,
  RCP<const Comm<int> > comm,
  FancyOStream& out)
{
  typedef Vector<double,int,int,node_type> Vector_t;
  typedef Map<int, int, node_type> Map_t;

  RCP<const Map_t> map = A->getRowMap();

  // Scaling vectors
  Teuchos::Array<typename Teuchos::ScalarTraits<double>::magnitudeType> norms(1);
  RCP<Vector_t> rightScaling = rcp( new Vector_t(B->getDomainMap()) );
  rightScaling->randomize();
  rightScaling->norm2(norms);
  rightScaling->scale(1.0/norms[0]);

  double omega = 1.0;
  Vector_t Dinv(B->getRowMap());
  Dinv.putScalar(1.0);

  // Jacobi version
  RCP<Matrix_t> computedC1 = rcp(new Matrix_t(B->getRowMap(), 0));
  Tpetra::MatrixMatrix::Jacobi(omega, Dinv, *A, *B, *computedC1);
  computedC1->rightScale(*rightScaling);

  // Bs = B * rightScaling
  RCP<node_type> node = KokkosClassic::Details::getNode<node_type>();
  RCP<Matrix_t> Bs = B->clone(node);
  Bs->rightScale(*rightScaling);

  // computedC2 = (I - Dinv*A)*Bs
  RCP<Matrix_t> computedC2 = rcp( new Matrix_t(computedC1->getCrsGraph()) );
  Tpetra::MatrixMatrix::Jacobi(omega, Dinv, *A, *Bs, *computedC2);

  // diffMatrix = computedC2 - computedC1
  RCP<Matrix_t> diffMatrix = Tpetra::createCrsMatrix<double, int, int, node_type>(computedC1->getRowMap());
  Tpetra::MatrixMatrix::Add(*computedC1, false, -1.0, *computedC2, false, 1.0, diffMatrix);
  diffMatrix->fillComplete(computedC1->getDomainMap(), computedC1->getRangeMap());

  mult_test_results results;
  results.cNorm    = computedC1->getFrobeniusNorm ();
  results.compNorm = diffMatrix->getFrobeniusNorm ();
  results.epsilon  = results.compNorm/results.cNorm;

  return results;
}



TEUCHOS_UNIT_TEST(Tpetra_MatMat, operations_test){
  using Teuchos::ParameterList;
  using std::endl;

  RCP<const Comm<int> > comm = DefaultPlatform::getDefaultPlatform().getComm();

  ParameterList defaultParameters;
  RCP<node_type> node = rcp(new node_type(defaultParameters));
  Teuchos::RCP<Teuchos::ParameterList> matrixSystems =
    Teuchos::getParametersFromXmlFile(matnamesFile);
  for (Teuchos::ParameterList::ConstIterator it = matrixSystems->begin();
       it != matrixSystems->end();
       ++it) {
    TEUCHOS_TEST_FOR_EXCEPTION(!it->second.isList(), std::runtime_error,
      "All top-level items in the list of matrix file names must be "
      "ParameterList instances.  " << endl <<
      "Bad tag's name: " << it->first <<
      "Type name: " << it->second.getAny().typeName() <<
      std::endl << std::endl);

    ParameterList currentSystem = matrixSystems->sublist (it->first);
    std::string name = currentSystem.name();
    std::string A_file = currentSystem.get<std::string> ("A");
    std::string B_file = currentSystem.get<std::string> ("B");
    std::string C_file = currentSystem.get<std::string> ("C");
    const bool AT = currentSystem.get<bool> ("TransA");
    const bool BT = currentSystem.get<bool> ("TransB");
    double epsilon = currentSystem.get<double> ("epsilon", defaultEpsilon);
    std::string op = currentSystem.get<std::string> ("op");

    RCP<Matrix_t> A = Reader<Matrix_t>::readSparseFile (A_file, comm, node);
    RCP<Matrix_t> B = Reader<Matrix_t>::readSparseFile (B_file, comm, node);
    RCP<Matrix_t> C = Reader<Matrix_t>::readSparseFile (C_file, comm, node);

    TEUCHOS_TEST_FOR_EXCEPTION(op != "multiply" && op != "add", std::runtime_error,
      "Unrecognized Matrix Operation: " << op);

    if (op == "multiply") {
      if (verbose)
        out << "Running multiply test for " << currentSystem.name() << endl;

      mult_test_results results = multiply_test(name, A, B, AT, BT, C, comm, out);

      if (verbose) {
        out << "Results:"     << endl;
        out << "\tEpsilon: "  << results.epsilon  << endl;
        out << "\tcNorm: "    << results.cNorm    << endl;
        out << "\tcompNorm: " << results.compNorm << endl;
      }
      TEST_COMPARE(results.epsilon, <, epsilon)

      if (verbose)
        out << "Running multiply reuse test for " << currentSystem.name() << endl;

      results = multiply_reuse_test(name, A, B, AT, BT, C, comm, out);

      if (verbose) {
        out << "Results:"     << endl;
        out << "\tEpsilon: "  << results.epsilon  << endl;
        out << "\tcNorm: "    << results.cNorm    << endl;
        out << "\tcompNorm: " << results.compNorm << endl;
      }
      TEST_COMPARE(results.epsilon, <, epsilon)


      // Do we try Jacobi?
      if (AT == false && BT == false && A->getDomainMap()->isSameAs(*A->getRangeMap())) {
        if (verbose)
          out << "Running jacobi test for " << currentSystem.name() << endl;

        results = jacobi_test(name, A, B, comm, out);
        if (verbose) {
          out << "Results:"     << endl;
          out << "\tEpsilon: "  << results.epsilon  << endl;
          out << "\tcNorm: "    << results.cNorm    << endl;
          out << "\tcompNorm: " << results.compNorm << endl;
        }
        TEST_COMPARE(results.epsilon, <, epsilon)

        if (verbose)
          out << "Running jacobi reuse test for " << currentSystem.name() << endl;

        results = jacobi_reuse_test(name, A, B, comm, out);

        if (verbose) {
          out << "Results:"     << endl;
          out << "\tEpsilon: "  << results.epsilon  << endl;
          out << "\tcNorm: "    << results.cNorm    << endl;
          out << "\tcompNorm: " << results.compNorm << endl;
        }
        TEST_COMPARE(results.epsilon, <, epsilon)
      }
    }
    else if (op == "add") {
      if (verbose)
        out << "Running 3-argument add test (nonnull C on input) for "
            << currentSystem.name() << endl;

      add_test_results results = regular_add_test(A, B, AT, BT, C, comm);

      TEST_COMPARE(results.epsilon, <, epsilon)
      out << "Regular Add Test Results: " << endl;
      out << "\tCorrect Norm: " << results.correctNorm << endl;
      out << "\tComputed norm: " << results.computedNorm << endl;
      out << "\tEpsilon: " << results.epsilon << endl;

      // FIXME (mfh 09 May 2013) This test doesn't currently pass.  I
      // don't think anyone ever exercised the case where C is null on
      // input before.  I'm disabling this test for now until I have a
      // chance to fix that case.
      if (verbose)
        out << "Running 3-argument add test (null C on input) for "
            << currentSystem.name() << endl;

      TEUCHOS_TEST_FOR_EXCEPTION(A.is_null (), std::logic_error,
                                 "Before null_add_test: A is null");
      TEUCHOS_TEST_FOR_EXCEPTION(B.is_null (), std::logic_error,
                                 "Before null_add_test: B is null");
      TEUCHOS_TEST_FOR_EXCEPTION(C.is_null (), std::logic_error,
                                 "Before null_add_test: C is null");

      results = null_add_test<Matrix_t> (*A, *B, AT, BT, *C, out);

      TEST_COMPARE(results.epsilon, <, epsilon)
      out << "Null Add Test Results: " << endl;
      out << "\tCorrect Norm: " << results.correctNorm << endl;
      out << "\tComputed norm: " << results.computedNorm << endl;
      out << "\tEpsilon: " << results.epsilon << endl;

      B = Reader<Matrix_t >::readSparseFile(B_file, comm, node, false);

      if (! BT) {
        if (verbose)
          out << "Running 2-argument add test for "
              << currentSystem.name() << endl;

        results = add_into_test(A,B,AT,C,comm);
        TEST_COMPARE(results.epsilon, <, epsilon)
        out << "Add Into Test Results: " << endl;
        out << "\tCorrect Norm: " << results.correctNorm << endl;
        out << "\tComputed norm: " << results.computedNorm << endl;
        out << "\tEpsilon: " << results.epsilon << endl;
      }
    }
  }
}

/*
 * This test was created at the request of Chris Siefert to verify
 * that some inexplicable behaviour in MueLu was not due to a faulty
 * assumption in the Matrix Matrix Multiply Kernel.
 * KLN 15/06/2011
 */
TEUCHOS_UNIT_TEST(Tpetra_MatMat, range_row_test){
  RCP<const Comm<int> > comm = DefaultPlatform::getDefaultPlatform().getComm();
  ParameterList defaultParameters;
  RCP<node_type> node = rcp(new node_type(defaultParameters));
  int numProcs = comm->getSize();
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //THIS NUMBER MUST BE EVEN SO THAT WHEN I CALCULATE THE NUMBER
  //OF ROWS IN THE DOMAIN MAP I DON'T ENCOUNTER ANY
  //WEIRD RESULTS DUE TO INTEGER DIVISION
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  int numRowsPerProc = 4;
  int rank = comm->getRank();
  global_size_t globalNumRows = numRowsPerProc*numProcs;

  RCP<CrsMatrix<double,int,int,node_type> > identityMatrix =
    getIdentityMatrix<double,int,int,int>(globalNumRows, comm, node);


//Create "B"
  Array<int> myRows = tuple<int>(
    rank*numRowsPerProc,
    rank*numRowsPerProc+1,
    rank*numRowsPerProc+2,
    rank*numRowsPerProc+3);
  Array<int> rangeElements;
  if(rank == 0){
    rangeElements = tuple<int>(
      (numProcs-1)*numRowsPerProc+1,
      (numProcs-1)*numRowsPerProc+2,
      (numProcs-1)*numRowsPerProc,
      (numProcs-1)*numRowsPerProc+3);
  }
  else{
    rangeElements = tuple<int>(
      (rank-1)*numRowsPerProc+1,
      (rank-1)*numRowsPerProc+2,
      (rank-1)*numRowsPerProc,
      (rank-1)*numRowsPerProc+3);
  }

  RCP<const Map<int,int,node_type> > bRowMap =
    Tpetra::createNonContigMapWithNode<int,int,node_type>(myRows, comm, node);
  RCP<const Map<int,int,node_type> > bRangeMap =
    Tpetra::createNonContigMapWithNode<int,int,node_type>(rangeElements, comm, node);
  //We divide by 2 to make the matrix tall and "skinny"
  RCP<const Map<int,int,node_type> > bDomainMap =
    Tpetra::createUniformContigMapWithNode<int,int,node_type>(
      globalNumRows/2, comm, node);
  RCP<CrsMatrix<double,int,int,node_type> > bMatrix =
    Tpetra::createCrsMatrix<double,int,int,node_type>(bRowMap, 1);
  for(
    ArrayView<const int>::iterator it =
      bRowMap->getNodeElementList().begin();
    it != bRowMap->getNodeElementList().end();
    ++it)
  {
    Array<int> col(1,(*it)/2);
    Array<double> val(1,3.0);
    bMatrix->insertGlobalValues(*it, col(), val());
  }
  bMatrix->fillComplete(bDomainMap, bRangeMap);

  out << "Regular I*P" << std::endl;
  mult_test_results results = multiply_test(
    "Different Range and Row Maps",
    identityMatrix,
    bMatrix,
    false,
    false,
    bMatrix,
    comm,
    out);
  if(verbose){
    out << "Results:" <<std::endl;
    out << "\tEpsilon: " << results.epsilon << std::endl;
    out << "\tcNorm: " << results.cNorm << std::endl;
    out << "\tcompNorm: " << results.compNorm << std::endl;
  }
  TEST_COMPARE(results.epsilon, <, defaultEpsilon)

  RCP<CrsMatrix<double,int,int,node_type> > identity2 =
    getIdentityMatrix<double,int,int,int>(globalNumRows/2, comm, node);

  RCP<const Map<int,int,node_type> > bTransRowMap =
    Tpetra::createUniformContigMapWithNode<int,int,node_type>(globalNumRows/2,comm,node);

  RCP<CrsMatrix<double,int,int,node_type> > bTrans =
    Tpetra::createCrsMatrix<double,int,int,node_type>(bTransRowMap, 1);
  Array<int> bTransRangeElements;
  if(rank == 0){
    bTransRangeElements = tuple<int>(
      (numProcs-1)*(numRowsPerProc/2)+1,
      (numProcs-1)*(numRowsPerProc/2));
  }
  else{
    bTransRangeElements = tuple<int>(
      (rank-1)*(numRowsPerProc/2)+1,
      (rank-1)*(numRowsPerProc/2));
  }
  out << bTransRangeElements << std::endl;
  RCP<const Map<int,int,node_type> > bTransRangeMap =
    Tpetra::createNonContigMapWithNode<int,int,node_type>(bTransRangeElements, comm, node);
  RCP<const Map<int,int,node_type> > bTransDomainMap =
    Tpetra::createUniformContigMapWithNode<int,int,node_type>(globalNumRows,comm,node);
  Tpetra::MatrixMatrix::Multiply(*identity2,false,*bMatrix, true, *bTrans, false);
  bTrans->fillComplete(bTransDomainMap, bTransRangeMap);

  RCP<CrsMatrix<double,int,int,node_type> > bTransTest =
    Tpetra::createCrsMatrix<double,int,int,node_type>(bTransRowMap, 1);

  for(
    ArrayView<const int>::iterator it =
      bRowMap->getNodeElementList().begin();
    it != bRowMap->getNodeElementList().end();
    ++it)
  {
    Array<int> col(1,*it);
    Array<double> val(1,3.0);
    bTransTest->insertGlobalValues((*it)/2, col(), val());
  }
  bTransTest->fillComplete(bTransDomainMap, bTransRangeMap);


  out << "Regular I*P^T" << std::endl;
  RCP<CrsMatrix<double,int,int,node_type> > bTransDiff =
    Tpetra::createCrsMatrix<double,int,int,node_type>(bTransRowMap, 1);
  Tpetra::MatrixMatrix::Add<double,int,int,node_type>(*bTransTest, false, -1.0, *bTrans, false, 1.0,bTransDiff);
  bTransDiff->fillComplete(bTransDomainMap, bDomainMap);
  double diffNorm = bTransDiff->getFrobeniusNorm ();
  double realNorm = bTransTest->getFrobeniusNorm ();
  double calcEpsilon = diffNorm/realNorm;

  out << "B" << std::endl;


  if(verbose){
    out << "Results:" <<std::endl;
    out << "\tEpsilon: " << calcEpsilon<< std::endl;
    out << "\treal norm: " << realNorm<< std::endl;
    out << "\tcompNorm: " << diffNorm<< std::endl;
  }
  TEST_COMPARE(calcEpsilon, <, defaultEpsilon)

}

/**
 * This test was written at the request of Chris Siefert
 * in order to verity that A^T * I produces correct results
 * when A's rowmap and rangemap are differnt.
 * KLN 23/06/2011
 */
TEUCHOS_UNIT_TEST(Tpetra_MatMat, ATI_range_row_test){
  RCP<const Comm<int> > comm = DefaultPlatform::getDefaultPlatform().getComm();
  ParameterList defaultParameters;
  RCP<node_type> node = rcp(new node_type(defaultParameters));
  int numProcs = comm->getSize();
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //THIS NUMBER MUST BE EVEN SO THAT WHEN I CALCULATE THE NUMBER
  //OF ROWS IN THE DOMAIN MAP I DON'T ENCOUNTER ANY
  //WEIRD RESULTS DUE TO INTEGER DIVISION
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  int numRowsPerProc = 4;
  int rank = comm->getRank();
  global_size_t globalNumRows = numRowsPerProc*numProcs;

//Create identity matrix
  RCP<CrsMatrix<double,int,int,node_type> > identityMatrix =
    getIdentityMatrix<double,int,int,int>(globalNumRows, comm, node);


  //Create A
  Array<int> aMyRows = tuple<int>(
    rank*numRowsPerProc,
    rank*numRowsPerProc+1,
    rank*numRowsPerProc+2,
    rank*numRowsPerProc+3);
  RCP<const Map<int,int,node_type> > aRowMap =
    Tpetra::createNonContigMapWithNode<int,int,node_type>(
      aMyRows, comm, node);
  RCP<const Map<int,int,node_type> > aDomainMap =
    Tpetra::createUniformContigMapWithNode<int,int,node_type>(
      globalNumRows/2, comm, node);
  Array<int> aRangeElements;
  if(rank == 0){
    aRangeElements = tuple<int>(
      (numProcs-1)*numRowsPerProc+1,
      (numProcs-1)*numRowsPerProc+2,
      (numProcs-1)*numRowsPerProc,
      (numProcs-1)*numRowsPerProc+3);
  }
  else{
    aRangeElements = tuple<int>(
      (rank-1)*numRowsPerProc+1,
      (rank-1)*numRowsPerProc+2,
      (rank-1)*numRowsPerProc,
      (rank-1)*numRowsPerProc+3);
  }
  RCP<const Map<int,int,node_type> > aRangeMap =
    Tpetra::createNonContigMapWithNode<int,int,node_type>(
      aRangeElements, comm, node);

  RCP<CrsMatrix<double,int,int,node_type> > aMat =
    Tpetra::createCrsMatrix<double,int,int,node_type>(aRowMap, 1);
  for(
    ArrayView<const int>::iterator it =
      aRowMap->getNodeElementList().begin();
    it != aRowMap->getNodeElementList().end();
    ++it)
  {
    Array<int> col(1,(*it)/2);
    Array<double> val(1,3.0);
    aMat->insertGlobalValues(*it, col(), val());
  }
  aMat->fillComplete(aDomainMap, aRangeMap);

  RowMatrixTransposer<double,int,int,node_type> transposer (aMat);
  RCP<CrsMatrix<double, int, int, node_type> > knownAMat =
    transposer.createTranspose();


  out << "Regular I*P" << std::endl;
  mult_test_results results = multiply_test(
    "Different Range and Row Maps",
    aMat,
    identityMatrix,
    true,
    false,
    knownAMat,
    comm,
    out);
  if(verbose){
    out << "Results:" <<std::endl;
    out << "\tEpsilon: " << results.epsilon << std::endl;
    out << "\tcNorm: " << results.cNorm << std::endl;
    out << "\tcompNorm: " << results.compNorm << std::endl;
  }
  TEST_COMPARE(results.epsilon, <, defaultEpsilon)

}

/*
 * This test was added at the request of Chris Siefert
 * Turns out it fails because the original algorithm in
 * EpetraExt doesn't try to handle the case where
 * A has non-unique rows in it's row map. Perhaps I will
 * come back and address that some day.
 * KLN 28/06/2011
 */
/*TEUCHOS_UNIT_TEST(Tpetra_MatMat, Multiple_row_owners){
  RCP<const Comm<int> > comm = DefaultPlatform::getDefaultPlatform().getComm();
  ParameterList defaultParameters;
  RCP<node_type> node = rcp(new node_type(defaultParameters));
  RCP<Matrix_t > A = Reader<Matrix_t >::readSparseFile("matrices/denserATa.mtx", comm, node);
  RCP<Matrix_t > C = Reader<Matrix_t >::readSparseFile("matrices/denserATc.mtx", comm, node, true, true);
  RowMatrixTransposer<double, int,int,node_type> transposer(*A);
  RCP<Matrix_t> AT = transposer.createTranspose();

  RCP<Matrix_t> importedC =
    Tpetra::createCrsMatrix<double, int, int, node_type>(AT->getRowMap());
  Tpetra::Import<int,int,node_type> importer(C->getRowMap(), importedC->getRowMap());
  importedC->doImport(*C, importer, Tpetra::REPLACE);
  importedC->fillComplete(AT->getDomainMap(), AT->getRangeMap());

  mult_test_results results = multiply_test(
    "AT*A with multipl row owners",
    AT,
    A,
    false,
    false,
    importedC,
    comm,
    out);
  if(verbose){
    out << "Results:" <<std::endl;
    out << "\tEpsilon: " << results.epsilon << std::endl;
    out << "\tcNorm: " << results.cNorm << std::endl;
    out << "\tcompNorm: " << results.compNorm << std::endl;
  }
  TEST_COMPARE(results.epsilon, <, defaultEpsilon)

}*/


} //namespace Tpetra



