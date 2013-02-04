/**
 * @brief C++ program to process out-of-core the halos and generate
 * a merger-tree.
 */

// C++ includes
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

// MPI include
#include <mpi.h>

// CosmologyTools includes
#include "ForwardHaloTracker.h"
#include "GenericIO.h"
#include "Halo.h"

//==============================================================================
// Global variables
//==============================================================================
int rank;
int size;
MPI_Comm comm = MPI_COMM_WORLD;

cosmologytools::ForwardHaloTracker *HaloTracker;

// Command line parameters
std::string DataPrefix;
std::string TimeStepsFile;
int MergerTreeThreshold;

std::vector< int > timesteps;
std::vector<cosmotk::Halo> Halos;
std::map<std::string,int> Halo2Idx;

//==============================================================================
// Function prototypes
//==============================================================================
void CartCommInit(MPI_Comm comm);
void RoundRobinAssignment(int numBlocks, std::vector<int> &assigned);
void ReadInAnalysisTimeSteps();
void ReadHalosAtTimeStep(int tstep);
int GetHaloIndex(int tstep,int haloTag);
void ReadBlock(int tstep, int blockIdx,
                 cosmotk::GenericIO &fofReader,
                 cosmotk::GenericIO &haloParticlesReader);

//==============================================================================
// Macros
//==============================================================================
#define PRINTLN( str ) {          \
  if(rank==0) {                    \
    std::cout << str << std::endl; \
    std::cout.flush();             \
  }                                \
  MPI_Barrier(comm);               \
}

#define PRINT( str ) {    \
  if(rank==0) {            \
    std::cout << str;      \
    std::cout.flush();     \
  }                        \
  MPI_Barrier(comm);       \
}

//------------------------------------------------------------------------------

/**
 * @brief Program main
 * @param argc the argument counter
 * @param argv the argument vector
 * @return rc return code
 */
int main(int argc, char **argv)
{
  // STEP 0: Initialize MPI
  MPI_Init(&argc,&argv);
  MPI_Comm_rank(comm,&rank);
  MPI_Comm_size(comm,&size);
  PRINTLN("- Initialize MPI...[DONE]");

  // STEP 1: Parse arguments
  if( argc != 4 )
    {
    std::cerr << "Usage: mpirun -n <NumProcs> "
              << "./halotracker <prefix> <timesteps.dat> <threshold>\n";
    MPI_Abort(comm,-1);
    }
  DataPrefix = std::string(argv[1]);
  TimeStepsFile = std::string(argv[2]);
  MergerTreeThreshold = atoi(argv[3]);

  // STEP 1: Get cartesian communicator, needed for GenericIO
  CartCommInit(comm);
  PRINTLN("- Initialize cartesian communicator...[DONE]");

  // STEP 2: Get time-steps
  ReadInAnalysisTimeSteps();
  PRINTLN("- Read in analysis time-steps...[DONE]");

  // STEP 3: Loop through all time-steps and track halos
  HaloTracker = new cosmologytools::ForwardHaloTracker();
  HaloTracker->SetCommunicator( comm );
  HaloTracker->SetMergerTreeThreshold( MergerTreeThreshold );
  for(int t=0; t < timesteps.size(); ++t)
    {
    ReadHalosAtTimeStep( timesteps[t] );

    // TODO: How do we get red-shift information ???
    HaloTracker->TrackHalos(t,-1.0,Halos);

    PRINTLN( "\t - Processed time-step " << t
             << "/" << timesteps.size()
             << " SIM TSTEP=" << timesteps[t] << "\n ");
    } // END for all time-step

  // STEP 4: Write the tree
  HaloTracker->WriteMergerTree("MergerTree.dat");

  // STEP 5: Finalize
  delete HaloTracker;

  MPI_Finalize();
  return 0;
}

//------------------------------------------------------------------------------
int GetHaloIndex(int tstep,int haloTag)
{
  std::string hashCode = cosmotk::Halo::GetHashCodeForHalo(haloTag,tstep);
  if( Halo2Idx.find(hashCode) != Halo2Idx.end() )
    {
    return( Halo2Idx[hashCode] );
    }
  else
    {
    cosmotk::Halo h;
    h.TimeStep = tstep;
    h.Tag      = haloTag;
    Halos.push_back( h );
    return( Halos.size()-1 );
    }
}

//------------------------------------------------------------------------------
void ReadBlock(int tstep, int blockIdx,
                 cosmotk::GenericIO &fofReader,
                 cosmotk::GenericIO &haloParticlesReader)
{
  int size = fofReader.readNumElems( blockIdx );
  //  Number of variables: 13
  //  fof_halo_count 4
  //  fof_halo_tag 4
  //  fof_halo_mass 4
  //  fof_halo_center_x 4
  //  fof_halo_center_y 4
  //  fof_halo_center_z 4
  //  fof_halo_mean_x 4
  //  fof_halo_mean_y 4
  //  fof_halo_mean_z 4
  //  fof_halo_mean_vx 4
  //  fof_halo_mean_vy 4
  //  fof_halo_mean_vz 4
  //  fof_halo_vel_disp 4

  // Do we need halo mass?
  // Data files store both center x,y,z and mean x,y,z. It is not clear
  // if we should choose one or the either, or both?
  // Do we need halo velocity dispression?
  int *haloTags = new int[size];
  POSVEL_T *center_x = new POSVEL_T[size];
  POSVEL_T *center_y = new POSVEL_T[size];
  POSVEL_T *center_z = new POSVEL_T[size];
  POSVEL_T *halo_vx  = new POSVEL_T[size];
  POSVEL_T *halo_vy  = new POSVEL_T[size];
  POSVEL_T *halo_vz  = new POSVEL_T[size];

  fofReader.addVariable("fof_halo_tag", haloTags, true);
  fofReader.addVariable("fof_halo_center_x", center_x, true);
  fofReader.addVariable("fof_halo_center_y", center_y, true);
  fofReader.addVariable("fof_halo_center_z", center_z, true);
  fofReader.addVariable("fof_halo_mean_vx", halo_vx, true);
  fofReader.addVariable("fof_halo_mean_vy", halo_vy, true);
  fofReader.addVariable("fof_halo_mean_vz", halo_vz, true);

  fofReader.readData(blockIdx,false,false);

  for( int i=0; i < size; ++i )
    {
    int tag = haloTags[i];
    int idx = GetHaloIndex(tstep,tag);
    Halos[idx].Center[0] = center_x[i];
    Halos[idx].Center[1] = center_y[i];
    Halos[idx].Center[2] = center_z[i];
    Halos[idx].AverageVelocity[0] = halo_vx[i];
    Halos[idx].AverageVelocity[1] = halo_vy[i];
    Halos[idx].AverageVelocity[2] = halo_vz[i];
    }

  delete [] haloTags;
  delete [] center_x;
  delete [] center_y;
  delete [] center_z;
  delete [] halo_vx;
  delete [] halo_vy;
  delete [] halo_vz;

  //  $ cat particletags.dat.output
  // - Initialize MPI...[DONE]
  // - Initialize cartesian communicator...[DONE]
  // Number of variables: 2
  // id 8
  // fof_halo_tag 8
  size = haloParticlesReader.readNumElems( blockIdx );
  ID_T *particleIds  = new ID_T[size];
  ID_T *halo_tags    = new ID_T[size];

  haloParticlesReader.addVariable("id",particleIds,true);
  haloParticlesReader.addVariable("fof_halo_tag",halo_tags,true);

  haloParticlesReader.readData(blockIdx,false,false);

  for(int i=0; i < size; ++i )
    {
    int idx = GetHaloIndex(tstep,halo_tags[i]);
    Halos[idx].ParticleIds.insert(particleIds[i]);
    }

  delete [] particleIds;
  delete [] halo_tags;

  fofReader.clearVariables();
  haloParticlesReader.clearVariables();
}

//------------------------------------------------------------------------------
void ReadHalosAtTimeStep(int tstep)
{
  // STEP 0: Construct file names
  std::ostringstream oss;
  oss.clear(); oss.str("");

  oss << DataPrefix << "." << tstep << ".fofproperties";
  std::string fofPropertiesFile = oss.str();

  oss.clear(); oss.str("");
  oss << DataPrefix << "." << tstep << ".haloparticletags";
  std::string haloParticlesFile = oss.str();

  // STEP 1: Open and read headers
  cosmotk::GenericIO FofPropertiesReader(comm,fofPropertiesFile);
  FofPropertiesReader.openAndReadHeader(false);

  cosmotk::GenericIO HaloParticlesReader(comm,haloParticlesFile);
  HaloParticlesReader.openAndReadHeader(false);
  assert(
   "pre: block mismatch between fof properties file and halo particles file" &&
   FofPropertiesReader.readNRanks()==HaloParticlesReader.readNRanks());

  // STEP 2: Round-robing assignment of blocks
  int numBlocks = FofPropertiesReader.readNRanks();
  std::vector<int> assignedBlocks;
  RoundRobinAssignment(numBlocks,assignedBlocks);

  // STEP 3: Loop through assigned blocks and read halos
  for(unsigned int blk=0; blk < assignedBlocks.size(); ++blk)
    {
    int blockIdx = assignedBlocks[ blk ];
    ReadBlock( tstep, blockIdx,FofPropertiesReader,HaloParticlesReader);
    } // END for all blocks
}

//------------------------------------------------------------------------------
void ReadInAnalysisTimeSteps()
{
  int numTimeSteps;
  std::ifstream ifs;
  switch(rank)
    {
    case 0:
      ifs.open(TimeStepsFile.c_str());
      if( !ifs.is_open() )
        {
        std::cerr << "Cannot open file " << TimeStepsFile << std::endl;
        MPI_Abort(comm,-1);
        }

      // Read in time-steps
      for(int tstep; ifs >> tstep; timesteps.push_back(tstep) );

      // Broadcast send total number of time-steps in the file
      numTimeSteps = timesteps.size();
      MPI_Bcast(&numTimeSteps,1,MPI_INTEGER,0,comm);

      // Broadcast send the time-steps
      MPI_Bcast(&timesteps[0],numTimeSteps,MPI_INTEGER,0,comm);
      break;
    default:
      // Broad cast receive the total numer of time-steps
      MPI_Bcast(&numTimeSteps,1,MPI_INTEGER,0,comm);

      // Allocate time-steps vector
      timesteps.resize(numTimeSteps);

      // Broad cast receive all the time-steps
      MPI_Bcast(&timesteps[0],numTimeSteps,MPI_INTEGER,0,comm);
    } // END switch

  // Barrier synchronization
  MPI_Barrier(comm);
}

//------------------------------------------------------------------------------
void RoundRobinAssignment(int numBlocks, std::vector<int> &assigned)
{
  if(size < numBlocks )
    {
    // round-robin assignment
    for(int blkIdx=0; blkIdx < numBlocks; ++blkIdx)
      {
      if( (blkIdx%size) == rank )
        {
        assigned.push_back(blkIdx);
        } // END if this process has this block
      } // END for all blocks in the file
    } // END if
  else if(size > numBlocks )
    {
    if( rank < numBlocks )
      {
      assigned.push_back(rank);
      }
    } // END else-if
  else
    {
    // one-to-one mapping
    assigned.push_back(rank);
    } // END else
}

//------------------------------------------------------------------------------
void CartCommInit(MPI_Comm mycomm)
{
  int periodic[] = { 1, 1, 1 };
  int reorder = 0;
  int dims[3] = { 0, 0, 0 };

  // Get cartesian dimensions
  MPI_Dims_create(size,3,dims);

  // Create cartesian communicator
  MPI_Comm cartComm;
  MPI_Cart_create(mycomm,3,dims,periodic,reorder,&cartComm);

  // Reset global variables for rank and comm
  MPI_Comm_rank(cartComm,&rank);
  comm=cartComm;
}