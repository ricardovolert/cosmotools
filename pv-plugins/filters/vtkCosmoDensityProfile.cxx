/*=========================================================================

 Program:   Visualization Toolkit
 Module:    vtkCosmoDensityProfile.cxx

 Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
 All rights reserved.
 See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

 This software is distributed WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the above copyright notice for more information.

 =========================================================================*/
#include "vtkCosmoDensityProfile.h"
#include "vtkObjectFactory.h"
#include "vtkIndent.h"
#include "vtkMultiProcessController.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkTable.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkUnstructuredGrid.h"
#include "vtkSphere.h"
#include "vtkSphereSource.h"
#include "vtkIdTypeArray.h"
#include "vtkCommunicator.h"
#include "vtkAlgorithm.h"
#include "vtkAlgorithmOutput.h"

#include <set>
#include <list>
#include <cassert>
#include <sstream>
#include <fstream>

namespace {

class vtkVectorSumOperator : public vtkCommunicator::Operation
{
  // Description:
  // Performs a "B+=A" vector operation.
  virtual void Function(
      const void *A, void *B, vtkIdType length, int datatype )
    {
      assert("pre: A vector is NULL" && (A != NULL) );
      assert("pre: B vector is NULL");
      vtkIdType idx = 0;
      switch( datatype )
        {
        case VTK_INT:
          {
          const int *aPtr = reinterpret_cast<const int*>(A);
          int *bPtr       = reinterpret_cast<int*>(B);
          for( ; idx < length; ++idx )
            {
            bPtr[ idx ] += aPtr[ idx ];
            }
          }
          break;
        case VTK_DOUBLE:
          {
          const double *aPtr = reinterpret_cast<const double*>(A);
          double *bPtr       = reinterpret_cast<double*>(B);
          for( ; idx < length; ++idx )
            {
            bPtr[ idx ] += aPtr[ idx ];
            }
          }
          break;
        default:
         std::cerr << "ERROR: Unresolved type! ";
         std::cerr << "Only VTK_INT and VTK_DOUBLE are supported\n";
        }
    }

  // Description:
  // Sets Commutative to true for this operation
  virtual int Commutative() { return 1; }
};

}

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkCosmoDensityProfile);

//------------------------------------------------------------------------------
vtkCosmoDensityProfile::vtkCosmoDensityProfile()
{
  this->Controller = vtkMultiProcessController::GetGlobalController();
  this->Center[0]  = this->Center[1] = this->Center[2] = 0.0;
  this->Radius     = 5.0;
  this->NumberOfSpheres = 0;
  this->UseFOFCenters   = 0;
  this->SetNumberOfInputPorts( 2 );
  this->SetNumberOfOutputPorts( 2 );
}

//------------------------------------------------------------------------------
vtkCosmoDensityProfile::~vtkCosmoDensityProfile()
{
  this->ConcentricRadii.clear();
  this->NumParticlesInSphere.clear();
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::PrintSelf(ostream &os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCosmoDensityProfile::FillInputPortInformation(
                      int port,vtkInformation *info)
{
  assert("pre: input information is NULL!" && (info != NULL) );
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
  if( port == 1 )
    {
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(),1);
    }
  return 1;
}

//------------------------------------------------------------------------------
int vtkCosmoDensityProfile::FillOutputPortInformation(
                      int port,vtkInformation *info)
{
  switch( port )
    {
    case 0:
      info->Set(vtkDataObject::DATA_TYPE_NAME(),"vtkMultiBlockDataSet");
      break;
    case 1:
      info->Set(vtkDataObject::DATA_TYPE_NAME(),"vtkTable");
      break;
    default:
      vtkErrorMacro("Undefined port!" << port );
    }
  return 1;
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::SetFOFCentersConnection(
    vtkAlgorithmOutput *algOutput)
{
  assert( "pre: Cannot connect to a NULL algorithm output!" &&
          (algOutput!=NULL) );
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::GenerateSpheres(
    int sphereIdx,
    double center[3],
    vtkMultiBlockDataSet *spheres)
{
  assert("pre: output multi-block dataset is NULL" && (spheres != NULL) );

  for( int i=0; i < this->NumberOfSpheres; ++i )
    {
    vtkIdType blockIdx = sphereIdx*this->NumberOfSpheres+i;
    vtkSphereSource *sphereGenerator = vtkSphereSource::New();
    sphereGenerator->SetCenter( center );
    sphereGenerator->SetRadius( this->ConcentricRadii[ i ] );
    sphereGenerator->Update();
    spheres->SetBlock( blockIdx, sphereGenerator->GetOutput() );
    sphereGenerator->Delete();
    } // END for all concentric spheres

}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::FindParticlesInSphere(
    const int radiusIdx,
    double center[3],
    vtkUnstructuredGrid* particles,
    std::list< vtkIdType > &particleIds )
{
  double r = this->ConcentricRadii[ radiusIdx ];
  vtkSphere *mySphere = vtkSphere::New();
  mySphere->SetCenter( center);
  mySphere->SetRadius( r );

  double pnt[3];
  if( radiusIdx == this->NumberOfSpheres-1 )
    {
    for( vtkIdType idx=0; idx < particles->GetNumberOfPoints(); ++idx )
      {
      particles->GetPoint( idx, pnt );
      if( mySphere->EvaluateFunction(pnt) <= 0.0 )
        {
        particleIds.push_front( idx );
        }
      } // END for all particles
    }
  else
    {
    std::set< vtkIdType > particlesToErase;
    std::list< vtkIdType >::iterator iter = particleIds.begin();
    for( ; iter != particleIds.end(); ++iter )
      {
      vtkIdType idx = *iter;
      particles->GetPoint(idx, pnt);
      if( mySphere->EvaluateFunction(pnt) > 0.0 )
        {
        particlesToErase.insert( idx );
        }
      } // END for all particle ids

    std::set< vtkIdType >::iterator setIter = particlesToErase.begin();
    for( ; setIter != particlesToErase.end(); ++setIter )
      {
      particleIds.remove( *setIter );
      } // END for all particle ids that are to be erased
    }

  mySphere->Delete();
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::ComputeDensity(
    const int sphereIdx,
    double center[3],
    vtkUnstructuredGrid* particles)
{
  int biggestRadiusId = this->NumberOfSpheres-1;
  std::list< vtkIdType > particleIds;

  for( int radiusIdx=biggestRadiusId; radiusIdx >= 0; --radiusIdx )
    {
    this->FindParticlesInSphere(radiusIdx,center,particles,particleIds );
    this->NumParticlesInSphere[ sphereIdx*this->NumberOfSpheres+radiusIdx ] =
        static_cast<int>( particleIds.size() );
    } // END for all spheres from bigger to smallest

  this->Controller->Barrier();

  // Reduce the sum for each sphere to process 0 which is responsible for
  // generating the final plot.
  if( this->Controller->GetNumberOfProcesses() > 1)
    {
    int *rcvBuffer = new int[this->NumberOfSpheres];
    assert("rcvBuffer is NULL!" && (rcvBuffer != NULL) );

    vtkVectorSumOperator vectorSumOperator;
    int rootProcess = 0;
    this->Controller->Reduce(
      &this->NumParticlesInSphere[sphereIdx*this->NumberOfSpheres],
      rcvBuffer,
      this->NumberOfSpheres,
      &vectorSumOperator,
      rootProcess);

    if( this->Controller->GetLocalProcessId() == rootProcess )
      {
      for( int i=0; i < this->NumberOfSpheres; ++i )
        {
        this->NumParticlesInSphere[sphereIdx*this->NumberOfSpheres+i] =
            rcvBuffer[i];
        } // END for all spheres
      } // END if rootProcess

    delete [] rcvBuffer;
    } // End if more than one process
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::ProcessSphereCenter(
    int sphereIdx,
    vtkUnstructuredGrid *particles,
    vtkMultiBlockDataSet *spheres)
{
  assert("pre: particles is NULL" && (particles != NULL) );
  assert("pre: spheres is NULL" && (spheres != NULL));

  // STEP 0: Get the sphere center
  double cntr[3];
  for( int i=0; i < 3; ++i )
    {
    cntr[i] = this->SphereCenters[ sphereIdx*3+i ];
    }

  // STEP 1: Generate concentric spheres at the given center
  this->GenerateSpheres( sphereIdx, cntr,spheres);

  // STEP 2: Compute the density for the given sphere
  this->ComputeDensity(sphereIdx, cntr, particles);
}

//------------------------------------------------------------------------------
void vtkCosmoDensityProfile::GetHaloFOFCenters(vtkUnstructuredGrid *fofCenters)
{
  this->NumberOfSphereCenters = fofCenters->GetNumberOfPoints();
  this->SphereCenters.resize( 3*this->NumberOfSphereCenters );

  std::cout << "Number of fofCenters: " << this->NumberOfSphereCenters;
  std::cout << std::endl;
  std::cout.flush();

  double pnt[3];
  for( int i=0; i < this->NumberOfSphereCenters; ++i )
    {
    fofCenters->GetPoint( i, pnt );
    this->SphereCenters[ i*3 ]   = pnt[0];
    this->SphereCenters[ i*3+1 ] = pnt[1];
    this->SphereCenters[ i*3+2 ] = pnt[2];
    } // END for all sphere centers
}

//------------------------------------------------------------------------------
int vtkCosmoDensityProfile::RequestData(
    vtkInformation* vtkNotUsed(request),vtkInformationVector** inputVector,
    vtkInformationVector *outputVector )
{
  // STEP 0: Get input object
  vtkInformation *input = inputVector[0]->GetInformationObject( 0 );
  assert("pre: input information object is NULL" && (input != NULL) );
  vtkUnstructuredGrid *particles =
      vtkUnstructuredGrid::SafeDownCast(
          input->Get( vtkDataObject::DATA_OBJECT() ) );
  assert("pre: input particles is NULL!" && (particles != NULL) );

  // STEP 1: Check to see if a second input is provided
  vtkUnstructuredGrid *fofCenters = NULL;
  vtkInformation *haloFinder = inputVector[1]->GetInformationObject(0);
  if( haloFinder != NULL )
    {
    std::cout << "USING FOF centers....\n";
    std::cout.flush();

    this->UseFOFCenters = 1;
    fofCenters = vtkUnstructuredGrid::SafeDownCast(
        haloFinder->Get( vtkDataObject::DATA_OBJECT() ) );
    assert("pre: input fof centers is NULL!" && (fofCenters != NULL) );
    }
  else
    {
    std::cout << "HALO FOFS are not provided...\n";
    std::cout.flush();
    }

  // STEP 2: Get the 1st output object
  vtkInformation *output = outputVector->GetInformationObject( 0 );
  assert( "pre: output information object is NULL" && (output != NULL) );
  vtkMultiBlockDataSet *mbds =
      vtkMultiBlockDataSet::SafeDownCast(
          output->Get( vtkDataObject::DATA_OBJECT() ) );

  // STEP 3: Get the 2nd output object
  vtkInformation *output2 = outputVector->GetInformationObject( 1 );
  assert( "pre: output information object is NULL" && (output2 != NULL) );
  vtkTable *plot = vtkTable::SafeDownCast(
      output2->Get(vtkDataObject::DATA_OBJECT() ) );

  // STEP 4: Set sphere centers to be processed
  if( this->UseFOFCenters == 1 )
    {
    this->GetHaloFOFCenters(fofCenters);
    }
  else
    {
    this->NumberOfSphereCenters = 1;
    this->SphereCenters.resize( 3 );
    this->SphereCenters[ 0 ] = this->Center[ 0 ];
    this->SphereCenters[ 1 ] = this->Center[ 1 ];
    this->SphereCenters[ 2 ] = this->Center[ 2 ];
    }

  // STEP 5: Allocate data-structures
  int Size = this->NumberOfSpheres*this->NumberOfSphereCenters;
  mbds->SetNumberOfBlocks( Size );
  this->NumParticlesInSphere.resize( Size );
  this->ConcentricRadii.resize( this->NumberOfSpheres );

  // STEP 6: Compute concentric radii
  double h = static_cast<double>(this->Radius/this->NumberOfSpheres );
  for( int i=0; i < this->NumberOfSpheres; ++i )
    {
    this->ConcentricRadii[ i ] = (i+1)*h;
    } // END for

  // STEP 7: Loop and process each center
  for( int sphereIdx=0; sphereIdx < this->NumberOfSphereCenters; ++sphereIdx )
    {
    this->ProcessSphereCenter(sphereIdx,particles,mbds);
    } // END for all sphere centers
  this->Controller->Barrier();

  // STEP 8: Add Density column to plot
  if( this->Controller->GetLocalProcessId( ) == 0 )
    {
    vtkIdTypeArray *sphereIds       = vtkIdTypeArray::New();
    sphereIds->SetName( "Sphere ID" );
    vtkIdTypeArray *sphereDensities = vtkIdTypeArray::New();
    sphereDensities->SetName("Density");

    for( int i=0; i < Size; ++i )
      {
      sphereIds->InsertNextValue( i );
      sphereDensities->InsertNextValue( this->NumParticlesInSphere[i] );
      } // END for all spheres
    plot->AddColumn( sphereIds );
    sphereIds->Delete();
    plot->AddColumn( sphereDensities );
    sphereDensities->Delete();
    }
  this->Controller->Barrier();
  return 1;
}
