#include "ParallelHaloMergerTree.h"

// cosmotools includes
#include "Halo.h"
#include "HaloNeighborExchange.h"
#include "HaloType.h"
#include "MPIUtilities.h"
#include "TaskTimer.h"

// DIY communication sub-strate
#include "diy.h"

#include <fstream>
#include <sstream>

namespace cosmotk
{

ParallelHaloMergerTree::ParallelHaloMergerTree()
{
  this->Communicator     = MPI_COMM_NULL;
  this->CurrentIdx       = this->PreviousIdx = -1;
  this->NumberOfNodes    = 0;
  this->NeighborExchange = new HaloNeighborExchange();
}

//------------------------------------------------------------------------------
ParallelHaloMergerTree::~ParallelHaloMergerTree()
{
  this->TemporalHalos.clear();

  if( this->NeighborExchange != NULL )
    {
    delete this->NeighborExchange;
    }
}

//------------------------------------------------------------------------------
void ParallelHaloMergerTree::UpdateMergerTree(
    const int t1, Halo *haloSet1, const int M,
    const int t2, Halo *haloSet2, const int N,
    DistributedHaloEvolutionTree *t)
{
  assert("pre: t1 < t2" && (t1 < t2) );
  assert("pre: halo evolution tree is NULL!" && (t != NULL) );
  assert("pre: HaloNeighborExchange object is NULL!" &&
          (this->NeighborExchange != NULL) );

  TaskTimer exchangeTimer, mtreeTimer, buildTreeTimer,handleDeathTimer;

  int nrank		  = 0;
  int averageSize = 0;
  if( this->Verbose )
    {
	MPI_Comm_size(this->Communicator,&nrank);
	int tot = M*N;
	MPIUtilities::SynchronizedPrintf(
			this->Communicator,"MATRIX: %d x %d (%d total)\n",M,N,tot);
	int sum = 0;
	MPI_Allreduce(&tot,&sum,1,MPI_INT,MPI_SUM,this->Communicator);
	averageSize = sum/nrank;
	MPIUtilities::Printf(
			this->Communicator,"AVERAGE MATRIX SIZE: %d\n", averageSize);
    } // END if Verbose


  this->NeighborExchange->SetCommunicator( this->Communicator );

  // STEP 0: Exchange halos -- This step set's up the TemporalHalos data-struct
  if( this->Verbose )
    {
	MPIUtilities::Printf(this->Communicator,"EXCHANGE HALOS...\n");
    }

  exchangeTimer.StartTimer();
  if( this->TemporalHalos.size() == 0 )
    {
    // This is the first time that we run the algorithm, so we must communicate
    // both time-steps
    this->TemporalHalos.resize(2);
    this->PreviousIdx = 0;
    this->CurrentIdx  = 1;

    this->AssignGlobalIds(haloSet1,M);
    this->NeighborExchange->ExchangeHalos(
        haloSet1,M,this->TemporalHalos[this->PreviousIdx]);

    this->AssignGlobalIds(haloSet2,N);
    this->NeighborExchange->ExchangeHalos(
        haloSet2,N,this->TemporalHalos[this->CurrentIdx]);
    }
  else
    {
    assert("pre: TemoralHalos.size()==2" && (this->TemporalHalos.size()==2));

    // Instead of copying vectors, we just swap indices to the TemporalHalos
    std::swap(this->CurrentIdx,this->PreviousIdx);

    // Exchange halos at the current time-step. Note!!!! halos at the
    // previous time-step were exchanged at the previous time-step.
    this->AssignGlobalIds(haloSet2,N);
    this->NeighborExchange->ExchangeHalos(
        haloSet2,N,this->TemporalHalos[this->CurrentIdx]);
    }
  exchangeTimer.StopTimer();
  if( this->Verbose )
    {
	MPIUtilities::Printf(
		   this->Communicator,"EXCHANGE HALOS [DONE] (%f)\n",
	   	   exchangeTimer.GetEllapsedTime());
    }

  // STEP 1: Register halos
  if( this->Verbose )
    {
	MPIUtilities::Printf(this->Communicator,"COMPUTE SIMILARITY MATRIX...\n");
    }

  mtreeTimer.StartTimer();
  this->RegisterHalos(
      t1,&this->TemporalHalos[this->PreviousIdx][0],
          this->TemporalHalos[this->PreviousIdx].size(),
      t2,&this->TemporalHalos[this->CurrentIdx][0],
          this->TemporalHalos[this->CurrentIdx].size());
  //this->PrintTemporalHalos();

  // STEP 2: Compute merger-tree
  this->ComputeMergerTree();
  mtreeTimer.StopTimer();
  if( this->Verbose )
    {
	MPIUtilities::Printf(
			this->Communicator,"SIMILARITY MATRIX [DONE] (%f)\n",
			mtreeTimer.GetEllapsedTime() );
    }

  //this->PrintMatrix( this->GetRank() );

  // STEP 3: Update the halo-evolution tree
  if( this->Verbose )
    {
	MPIUtilities::Printf(this->Communicator,"BUILD MERGER TREE...\n");
    }

  buildTreeTimer.StartTimer();
  this->UpdateHaloEvolutionTree( t );
  buildTreeTimer.StopTimer();
  if( this->Verbose )
    {
	MPIUtilities::Printf(
			this->Communicator,"BUILD MERGER TREE [DONE] (%f)\n",
			buildTreeTimer.GetEllapsedTime());
    }

  // STEP 4: Print diagnostics for this interval
  if( this->Verbose )
    {
    MPIUtilities::Printf(
     this->Communicator,"=============================\n");
    MPIUtilities::Printf(
     this->Communicator,"TIMESTEP INTERVAL [%d,%d]\n",t1,t2);
    MPIUtilities::Printf(
     this->Communicator,"TOTAL NUMBER OF BIRTHS: %d\n",
     this->GetTotalNumberOfBirths());
    MPIUtilities::Printf(
     this->Communicator,"TOTAL NUMBER OF RE-BIRTHS: %d\n",
     this->GetTotalNumberOfRebirths());
    MPIUtilities::Printf(
     this->Communicator,"TOTAL NUMBER OF MERGERS: %d\n",
     this->GetTotalNumberOfMerges());
    MPIUtilities::Printf(
     this->Communicator,"TOTAL NUMBER OF SPLITS: %d\n",
     this->GetTotalNumberOfSplits());
    MPIUtilities::Printf(
     this->Communicator,"TOTAL NUMBER OF DEATHS: %d\n",
     this->GetTotalNumberOfDeaths());
    MPIUtilities::Printf(
     this->Communicator,"=============================\n");
    }

  // STEP 5: Handle Death events
  if( this->Verbose )
    {
	MPIUtilities::Printf(this->Communicator,"HANDLE DEATH EVENTS...\n");
    }

  handleDeathTimer.StartTimer();
  this->HandleDeathEvents( t );
  handleDeathTimer.StopTimer();

  if( this->Verbose )
    {
	MPIUtilities::Printf(
			this->Communicator,"HANDLE DEATH EVENTS [DONE] (%f)",
			handleDeathTimer.GetEllapsedTime());
    }

  // STEP 6: Barrier synchronization
  this->Barrier();
}

//------------------------------------------------------------------------------
void ParallelHaloMergerTree::HandleDeathEvents(
        DistributedHaloEvolutionTree *t)
{
  assert("pre: halo evolution tree is NULL!" && (t != NULL) );

  // The zombie idx vector keeps the indices of zombies that are injected.
  std::vector<int> zombieidx;
  std::vector<int> sourceidx;

  // STEP 0: Inject zombies to the next time-step
  std::set< int >::iterator iter = this->DeadHalos.begin();
  for(;iter != this->DeadHalos.end(); ++iter)
    {
    int haloIdx = *iter;

    // Sanity checks
    assert("pre: Dead haloIdx is out-of-bounds" &&
        (haloIdx >= 0) &&
        (haloIdx < this->TemporalHalos[this->PreviousIdx].size()) );

    Halo *sourceHalo = &this->TemporalHalos[this->PreviousIdx][haloIdx];
    if( HaloType::IsType(sourceHalo->HaloTypeMask,HaloType::GHOST) )
      {
      // skip ghost halos, they will be injected in the process that owns them
      continue;
      }

    sourceidx.push_back(haloIdx);

    // Copy the halo
    this->TemporalHalos[this->CurrentIdx].push_back(
          this->TemporalHalos[this->PreviousIdx][haloIdx]);
    int zombieIdx = this->TemporalHalos[this->CurrentIdx].size()-1;

    // Update zombie idx vector
    zombieidx.push_back( zombieIdx );

    // Get pointers to the zombie halo at the current timestep and the
    // source halo at the previous timestep, i.e., the halo that died.
    Halo *zombie     = &this->TemporalHalos[this->CurrentIdx][zombieIdx];

    // Get a pointer to a reference halo at the current timestep so that we
    // can update the timestep and red-shift information.
    Halo *refHalo    = &this->TemporalHalos[this->CurrentIdx][0];
    zombie->TimeStep = refHalo->TimeStep;
    zombie->Redshift = refHalo->Redshift;
    if( zombie->Count == 0 )
      {
      // this halo just died for the first time
      HaloType::SetType(zombie->HaloTypeMask,HaloType::ZOMBIE);
      zombie->Tag = (-1)*zombie->Tag;
      }

    // More sanity checks
    assert("pre: Node not flagged as a zombie!" &&
           (HaloType::IsType(zombie->HaloTypeMask,HaloType::ZOMBIE)));
    assert("pre: zombies must have a negative tag!" && (zombie->Tag < 0));
    zombie->Count++;
    } // END for all dead halos

  assert("ERROR: number of zombies in the current time-step must match" &&
          zombieidx.size()==sourceidx.size() );

  // STEP 1: Assign global IDs to zombies
  this->AssignGlobalIdsToZombieNodes(&zombieidx[0],zombieidx.size());

  // STEP 2: Update local merger-tree
  for(unsigned int idx=0; idx < zombieidx.size(); ++idx)
    {
    int zombieIdx  = zombieidx[ idx ];
    int srcHaloIdx = sourceidx[ idx ];
    assert("pre: Source haloIdx is out-of-bounds" &&
      (srcHaloIdx >= 0) &&
      (srcHaloIdx < this->TemporalHalos[this->PreviousIdx].size()));
    assert("pre: Dead haloIdx is out-of-bounds" &&
      (zombieIdx >= 0) &&
      (zombieIdx < this->TemporalHalos[this->CurrentIdx].size()));

    Halo* zombie     = &this->TemporalHalos[this->CurrentIdx][zombieIdx];
    Halo* sourceHalo = &this->TemporalHalos[this->PreviousIdx][srcHaloIdx];

    // More sanity checks
    assert("pre: source halo is NULL!" && (sourceHalo != NULL) );
    assert("pre: zombie halo is NULL!" && (zombie != NULL));
    assert("pre: source halo does not have a global ID" &&
        sourceHalo->HasGlobalID());
    assert("pre: zombie halo does not have a global ID" &&
        zombie->HasGlobalID());
    assert("pre: Node not flagged as a zombie!" &&
        (HaloType::IsType(zombie->HaloTypeMask,HaloType::ZOMBIE)));
    assert("pre: zombies must have a negative tag!" && (zombie->Tag < 0));

    unsigned char bitmask;
    MergerTreeEvent::Reset(bitmask);
    MergerTreeEvent::SetEvent(bitmask,MergerTreeEvent::DEATH);

    this->InsertHalo(zombie,bitmask,t);
    t->LinkHalos(sourceHalo,zombie);
    } // END for all zombies

  // STEP 3: Enqueue zombie halos to exchange
  for(unsigned int idx=0; idx < zombieidx.size(); ++idx)
    {
    Halo* zombie = &this->TemporalHalos[this->CurrentIdx][zombieidx[idx]];
    assert("pre: not marked as zombie!" &&
           HaloType::IsType(zombie->HaloTypeMask,HaloType::ZOMBIE));
    assert("pre: zombie count >= 1" && (zombie->Count >= 1) );
    this->NeighborExchange->EnqueueHalo( zombie );
    }

  // STEP 4: Exchanged zombies
  std::vector< Halo > neiZombies;
  this->NeighborExchange->ExchangeHalos(neiZombies,false);

  // STEP 5: Inject the neighboring zombies to the temporal data-structure
  for(unsigned int z=0; z < neiZombies.size(); ++z)
    {
    assert( "pre: not marked as zombie!" &&
       HaloType::IsType(neiZombies[z].HaloTypeMask,HaloType::ZOMBIE));
    assert( "pre: not marked as ghost!" &&
       HaloType::IsType(neiZombies[z].HaloTypeMask,HaloType::GHOST));

    this->TemporalHalos[this->CurrentIdx].push_back(neiZombies[z]);
    } // END for all zombies of neighboring processes
}

//------------------------------------------------------------------------------
void ParallelHaloMergerTree::AssignGlobalIdsToZombieNodes(
        int* zombieIdList, const int N)
{
  // STEP 0: Get unique range in this process
  ID_T range[2];
  MPIUtilities::GetProcessRange(this->Communicator,N,range);

  // STEP 1: Assign global IDs to zombies
  ID_T globalIdx = range[0];
  for(int i=0; i < N; ++i, ++globalIdx)
    {
    int zombieIdx = zombieIdList[ i ];
    assert("pre: zombie index is out-of-bounds!" &&
         (zombieIdx >= 0) &&
         (zombieIdx < this->TemporalHalos[this->CurrentIdx].size() ) );

    Halo* zombie  = &this->TemporalHalos[this->CurrentIdx][zombieIdx];
    assert("pre: zombie halo is NULL!" && (zombie != NULL));

    zombie->GlobalID = globalIdx+this->NumberOfNodes;
    } // END for all zombies
  assert("ERROR: globalIdx > range[1] detected!" && (globalIdx == range[1]+1) );

  // STEP 2: Compute the last global index assigned to a halo
  ID_T localGlobalIdx = range[1]+this->NumberOfNodes;
  ID_T lastGlobalIdx = -1;
  MPI_Allreduce(
      &localGlobalIdx,&lastGlobalIdx,1,MPI_ID_T,MPI_MAX,this->Communicator);

  // STEP 3: Update the total number of nodes
  this->NumberOfNodes = lastGlobalIdx+1;
}

//------------------------------------------------------------------------------
void ParallelHaloMergerTree::AssignGlobalIds(
            Halo* halos, const int numHalos)
{
  // STEP 0: Get unique range in this process
  ID_T range[2];
  MPIUtilities::GetProcessRange(this->Communicator,numHalos,range);

  // STEP 1: Compute global IDs for each halo, across all time-steps
  ID_T globalIdx = range[0];
  for(int i=0; i < numHalos; ++i, ++globalIdx )
    {
    halos[ i ].GlobalID = globalIdx+this->NumberOfNodes;
    }
  assert("ERROR: globalIdx > range[1] detected!" && (globalIdx == range[1]+1));

  // STEP 2: Compute the last global Index assigned to a halo
  ID_T localGlobalIdx = range[1]+this->NumberOfNodes;
  ID_T lastGlobalIdx = -1;
  MPI_Allreduce(
      &localGlobalIdx,&lastGlobalIdx,1,MPI_ID_T,MPI_MAX,this->Communicator);

  // STEP 3: Update the total number of nodes
  this->NumberOfNodes = lastGlobalIdx+1;

}

//------------------------------------------------------------------------------
void ParallelHaloMergerTree::PrintTemporalHalos()
{
  std::ostringstream oss;

  oss << "# " << this->Timesteps[0] << " "
      << this->TemporalHalos[0].size() << std::endl;
  for(unsigned int idx=0; idx < this->TemporalHalos[0].size(); ++idx)
    {
    oss << this->TemporalHalos[0][idx].GlobalID;
    if( HaloType::IsType(
        this->TemporalHalos[0][idx].HaloTypeMask,HaloType::GHOST))
      {
      oss << "(G)";
      }
    oss << "\t";
    } // END for
  oss << std::endl;

  oss << "# " << this->Timesteps[1] << " "
      << this->TemporalHalos[1].size() << std::endl;
  for(unsigned int idx=0; idx < this->TemporalHalos[1].size(); ++idx)
    {
    oss << this->TemporalHalos[1][idx].GlobalID;
    if( HaloType::IsType(
        this->TemporalHalos[1][idx].HaloTypeMask,HaloType::GHOST))
      {
      oss << "(G)";
      }
    oss << "\t";
    } // END for
  oss << std::endl;

  std::ostringstream fileStream;
  fileStream << "Rank-" << this->GetRank() << "-TemporalHalos.dat";

  std::ofstream ofs;
  ofs.open(fileStream.str().c_str());
  ofs << oss.str();
  ofs.close();
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfHalos()
{
  int nHalos = 0;
  if( this->TemporalHalos.size()==2 )
    {
    nHalos += this->TemporalHalos[ 0 ].size();
    nHalos += this->TemporalHalos[ 1 ].size();
    }

  int nTotal = 0;
  MPI_Allreduce(&nHalos,&nTotal,1,MPI_INT,MPI_SUM,this->Communicator);

  return(nTotal);
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfHaloParticles()
{
 int nHaloParticles = 0;
 if( this->TemporalHalos.size()==2)
   {
   for(int t=0; t < 2; ++t )
     {
     for(unsigned int hidx=0; hidx < this->TemporalHalos[t].size(); ++hidx)
       {
       nHaloParticles += this->TemporalHalos[t][hidx].ParticleIds.size();
       } // END for all previous halos
     } // END for all timesteps
   } // END if

 int nTotalHaloParticles = 0;
 MPI_Allreduce(
    &nHaloParticles,&nTotalHaloParticles,1,MPI_INT,MPI_SUM,this->Communicator);

 return(nTotalHaloParticles);
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfBirths()
{
  int total = 0;
  MPI_Allreduce(
      &this->NumberOfBirths,&total,1,MPI_INT,MPI_SUM,this->Communicator);
  return( total );
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfRebirths()
{
  int total = 0;
  MPI_Allreduce(
      &this->NumberOfRebirths,&total,1,MPI_INT,MPI_SUM,this->Communicator);
  return( total );
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfMerges()
{
  int total = 0;
  int local = this->MergeHalos.size();
  MPI_Allreduce(&local,&total,1,MPI_INT,MPI_SUM,this->Communicator);
  return( total );
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfSplits()
{
  int total = 0;
  int local = this->SplitHalos.size();
  MPI_Allreduce(&local,&total,1,MPI_INT,MPI_SUM,this->Communicator);
  return( total );
}

//------------------------------------------------------------------------------
int ParallelHaloMergerTree::GetTotalNumberOfDeaths()
{
  int total = 0;
  int local = this->DeadHalos.size();
  MPI_Allreduce(&local,&total,1,MPI_INT,MPI_SUM,this->Communicator);
  return( total );
}

} /* namespace cosmotk */
