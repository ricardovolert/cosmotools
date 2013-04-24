#include "DistributedHaloEvolutionTree.h"

// CosmologyTools includes
#include "Halo.h"
#include "MergerTreeEvent.h"

// STL includes
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <set>
#include <limits>

//------------------------------------------------------------------------------

namespace cosmotk
{

//------------------------------------------------------------------------------
DistributedHaloEvolutionTree::DistributedHaloEvolutionTree()
{
  this->Communicator     = MPI_COMM_NULL;
  this->NumberOfMergers  = 0;
  this->NumberOfRebirths = 0;
  this->NumberOfSplits   = 0;
  this->NumberOfZombies  = 0;
}

//------------------------------------------------------------------------------
DistributedHaloEvolutionTree::~DistributedHaloEvolutionTree()
{
  this->Clear();
}

//------------------------------------------------------------------------------
void DistributedHaloEvolutionTree::InsertNode(
      const HaloInfo &halo, unsigned char mask)
{
  assert("pre: corrupted merger-tree" && this->EnsureArraysAreConsistent());

  // STEP 0: Get hash code for node
  std::string hashCode = Halo::GetHashCodeForHalo(halo.Tag,halo.TimeStep);
  assert("pre: Encountered duplicate tree node!" && !this->HasNode(hashCode));

  // STEP 1: Insert node to list and create node-to-index mapping
  this->Nodes.push_back( halo );
  this->Node2Idx[ hashCode ] = halo.GlobalID;

  // STEP 2: Initialize progenitor and descendant lists for the node
  std::vector< int > myProgenitors;
  myProgenitors.resize( 0 );
  std::vector< int > myDescendants;
  myDescendants.resize( 0 );
  this->Progenitors.push_back( myProgenitors );
  this->Descendants.push_back( myDescendants );

  // STEP 3: Store node event bitmask
  this->EventBitMask.push_back( mask );

  // STEP 4: Update various statistics
  this->UpdateNodeCounter(halo.TimeStep);

  if( MergerTreeEvent::IsEvent(mask,MergerTreeEvent::DEATH) )
    {
    ++this->NumberOfZombies;
    this->UpdateZombieCounter(halo.TimeStep);
    }

  if( MergerTreeEvent::IsEvent(mask,MergerTreeEvent::MERGE))
    {
    ++this->NumberOfMergers;
    }

  if( MergerTreeEvent::IsEvent(mask,MergerTreeEvent::REBIRTH) )
    {
    ++this->NumberOfRebirths;
    }

  if( MergerTreeEvent::IsEvent(mask,MergerTreeEvent::SPLIT) )
    {
    ++this->NumberOfSplits;
    }

  assert("post: corrupted merger-tree" && this->EnsureArraysAreConsistent());
}

//------------------------------------------------------------------------------
void DistributedHaloEvolutionTree::LinkHalos(
      const std::string oldHalo,const std::string newHalo)
{
  assert("pre: corrupted merger-tree" && this->EnsureArraysAreConsistent());
  assert("pre: old halo does not exist" && this->HasNode(oldHalo) );
  assert("pre: new halo does not exist" && this->HasNode(newHalo) );

  // STEP 0: Get indices for new & old halo
  int oldIdx = this->Node2Idx[ oldHalo ];
  int newIdx = this->Node2Idx[ newHalo ];

  // STEP 1: Update descendants of oldHalo
  this->Descendants[ oldIdx ].push_back( newIdx );

  // STEP 2: Update progenitors of newHalo
  this->Progenitors[ newIdx ].push_back( oldIdx );

  assert("post: corrupted merger-tree" && this->EnsureArraysAreConsistent());
}

//------------------------------------------------------------------------------
void DistributedHaloEvolutionTree::Clear()
{
  this->Nodes.clear();
  this->Progenitors.clear();
  this->Descendants.clear();
  this->EventBitMask.clear();
  this->Node2Idx.clear();
  this->NodeCounter.clear();
  this->ZombieCounter.clear();
}

//------------------------------------------------------------------------------
int DistributedHaloEvolutionTree::GetNumberOfNodes(const int tstep)
{
  int N = 0;
  if(this->NodeCounter.find(tstep) != this->NodeCounter.end() )
    {
    N = this->NodeCounter[ tstep ];
    }
  else
    {
    std::cerr << "WARNING: No tree nodes for requrested time-step!\n";
    }
  return( N );
}

//------------------------------------------------------------------------------
int DistributedHaloEvolutionTree::GetNumberOfZombieNodes(
    const int tstep)
{
  int N = 0;
  if( this->ZombieCounter.find(tstep) != this->ZombieCounter.end() )
    {
    N = this->ZombieCounter[ tstep ];
    }
  return( N );
}

//------------------------------------------------------------------------------
bool DistributedHaloEvolutionTree::HasNode(std::string hashCode)
{
  if( this->Node2Idx.find(hashCode) == this->Node2Idx.end() )
    {
    return false;
    }
  return true;
}

//------------------------------------------------------------------------------
void DistributedHaloEvolutionTree::UpdateNodeCounter(const int tstep)
{
  if( this->NodeCounter.find(tstep) != this->NodeCounter.end() )
    {
    this->NodeCounter[tstep]++;
    }
  else
    {
    this->NodeCounter[tstep] = 1;
    }
}

//-----------------------------------------------------------------------------
void DistributedHaloEvolutionTree::UpdateZombieCounter(const int tstep)
{
  if( this->ZombieCounter.find(tstep) != this->ZombieCounter.end() )
    {
    this->ZombieCounter[tstep]++;
    }
  else
    {
    this->ZombieCounter[tstep] = 1;
    }
}

//------------------------------------------------------------------------------
std::string DistributedHaloEvolutionTree::ToString()
{
  assert("pre: corrupted merger-tree" && this->EnsureArraysAreConsistent());

  std::ostringstream oss;
  oss << "NUMNODES " << this->GetNumberOfNodes() << std::endl;
  oss << "ID\t";
  oss << "TIMESTEP\t";
  oss << "TAG\t";
  oss << "REDSHIFT\t";
  oss << "CENTER_X\t";
  oss << "CENTER_Y\t";
  oss << "CENTER_Z\t";
  oss << "MEAN_CENTER_X\t";
  oss << "MEAN_CENTER_Y\t";
  oss << "MEAN_CENTER_Z\t";
  oss << "V_X\t";
  oss << "V_Y\t";
  oss << "V_Z\t";
  oss << "MASS\t";
  oss << "DESCENDANTS\t";
  oss << "PROGENITORS\t";
  oss << "EVENT_TYPE\t";
  oss << std::endl;

  for(int i=0; i < this->GetNumberOfNodes(); ++i)
    {
    oss << std::scientific
        << std::setprecision(std::numeric_limits<POSVEL_T>::digits10);
    oss << i << "\t";
    oss << this->Nodes[ i ].TimeStep  << "\t";
    oss << this->Nodes[ i ].Tag       << "\t";
    oss << this->Nodes[ i ].Redshift  << "\t";
    oss << this->Nodes[ i ].Center[0] << "\t";
    oss << this->Nodes[ i ].Center[1] << "\t";
    oss << this->Nodes[ i ].Center[2] << "\t";
    oss << this->Nodes[ i ].MeanCenter[ 0 ] << "\t";
    oss << this->Nodes[ i ].MeanCenter[ 1 ] << "\t";
    oss << this->Nodes[ i ].MeanCenter[ 2 ] << "\t";
    oss << this->Nodes[ i ].AverageVelocity[ 0 ] << "\t";
    oss << this->Nodes[ i ].AverageVelocity[ 1 ] << "\t";
    oss << this->Nodes[ i ].AverageVelocity[ 2 ] << "\t";
    oss << this->Nodes[ i ].HaloMass << "\t";

    oss << "[ ";
    for(int didx=0; didx < this->Descendants[i].size(); ++didx)
      {
      oss << this->Descendants[i][didx] << " ";
      } // END for all descendants

    // Put a -1 if there are no descendants. Note, this
    // should only happen at the very final time-step.
    if( this->Descendants[i].size() == 0 )
      {
      oss << "-1 ";
      }
    oss << "]\t";

    oss << "[ ";
    for(int pidx=0; pidx < this->Progenitors[i].size(); ++pidx)
      {
      oss << this->Progenitors[i][pidx] << " ";
      }

    // Put -999 here to indicate an empty progenitor list.
    if( this->Progenitors[i].size() == 0 )
      {
      oss << "-999 ";
      }
    oss << "]\t";

    unsigned char bitmask = this->EventBitMask[ i ];
    oss << MergerTreeEvent::GetEventString(bitmask);

    if( this->Descendants[i].size()==0 )
      {
      oss << "(**FINAL TIMESTEP**)";
      }

    oss << std::endl;
    } // END for all nodes

  return( oss.str() );
}


} /* namespace cosmotk */
