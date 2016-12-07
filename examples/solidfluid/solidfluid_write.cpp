/*
 * globalArrayXML.cpp
 *
 *  Created on: Oct 31, 2016
 *      Author: pnorbert
 */

#include <stdexcept>
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "public/ADIOS.h"


struct MYDATA {
    int NX;
    double *t;
    std::vector<double> p;
};

const int N = 10;
struct MYDATA solid, fluid;
MPI_Comm    comm=MPI_COMM_WORLD;
int         rank, size;


void write_data (adios::ADIOS adios, struct MYDATA &data, ADIOS_OUTPUT &outfile)
{
    adios.Write (outfile, "NX", &data.NX);
    adios.Write (outfile, "rank", &rank);
    adios.Write (outfile, "size", &size);
    adios.Write (outfile, "temperature", data.t);
    adios.Write (outfile, "pressure", data.p);
    //adios.Write (outfile, true); // true: advance step, this is default value
    adios.Write (outfile);
}

void write_ckpt (struct MYDATA &solid, struct MYDATA &fluid, adios::ADIOS_OUTPUT &ckptfile)
{
    try {
        // Open an output for a Group
        // a transport or a processor should be associated with the group
        ckptfile.Open("ckpt.bp");

        ckptfile.ScheduleWrite ("solid/NX", &solid.NX);
        ckptfile.ScheduleWrite ("solid/rank", &rank);
        ckptfile.ScheduleWrite ("solid/size", &size);
        ckptfile.ScheduleWrite ("solid/temperature", solid.t);
        ckptfile.ScheduleWrite ("solid/pressure", solid.p);

        ckptfile.ScheduleWrite ("fluid/NX", &fluid.NX);
        ckptfile.ScheduleWrite ("fluid/rank", &rank);
        ckptfile.ScheduleWrite ("fluid/size", &size);
        ckptfile.ScheduleWrite ("fluid/temperature", fluid.t);
        ckptfile.ScheduleWrite ("fluid/pressure", fluid.p);

        ckptfile.Write();
        ckptfile.Close(); // Should this do Write() if user misses or should we complain?
    }
    catch ( std::exception& e ) //need to think carefully how to handle C++ exceptions with MPI to avoid deadlocking
    {
        if( rank == 0 )
        {
            std::cout << e.what() << "\n";
        }
    }
}

void write_viz (struct MYDATA &solid, struct MYDATA &fluid, ADIOS_OUTPUT &vizstream)
{
    // This stream is not associated with a group, so we must say for each write which group to use
    // The output variable is re-defined inside as <groupname>/<varname>, unless given as third string argument
    // An array variable's dimension definitions are also re-defined with dimensions <groupname>/<dimensionvar>
    adios.Write (vizstream, "solid", "NX", &solid.NX);
    adios.Write (vizstream, "solid", "rank", &rank);
    adios.Write (vizstream, "solid", "size", &size);
    // write solid group's temperature simply as temperature, risking overloading the 'temperature' variable when
    // reading from a file
    adios.Write (vizstream, "solid", "temperature", "my/tempvarinfile", solid.t);

    adios.Write (vizstream, "fluid", "NX", &fluid.NX);
    adios.Write (vizstream, "fluid", "rank", &rank);
    adios.Write (vizstream, "fluid", "size", &size);

    adios.Write (vizstream, "fluid", "temperature", "temperature", fluid.t);

    vizstream.Write();
    vizstream.AdvanceStep();
}

void compute (struct MYDATA &solid, struct MYDATA &fluid)
{
    for (int i = 0; i < solid.NX; i++)
    {
        solid.t[i] = it*100.0 + rank*solid.NX + i;
        solid.p[i] = it*1000.0 + rank*solid.NX + i;
    }
    for (int i = 0; i < fluid.NX; i++)
    {
        fluid.t[i] = it*200.0 + rank*fluid.NX + i;
        fluid.p[i] = it*2000.0 + rank*fluid.NX + i;
    }
}

int main( int argc, char* argv [] )
{
    MPI_Init (&argc, &argv);
    MPI_Comm_rank (comm, &rank);
    MPI_Comm_size (comm, &size);

    solid.NX = N;
    solid.t = new double(N);
    solid.p = std::vector<double>(N);

    fluid.NX = N;
    fluid.t = new double(N);
    fluid.p = std::vector<double>(N);

    try
    {
        // ADIOS manager object creation. MPI must be initialized
        adios::ADIOS adios( "globalArrayXML.xml", comm, true );

        // Open a file with a Method which has selected a group and a transport in the XML.
        // "a" will append to an already existing file, "w" would create a new file
        // Multiple writes to the same file work as append in this application run
        // FIXME: how do we support Update to same step?

        adios::ADIOS_OUTPUT solidfile( comm, "a", "solid"); // "solid" is a method but incidentally also a group
        // Constructor only creates an object and what is needed there but does not open a stream/file
        // It can be used to initialize a staging connection if not declared before
        // FIXME: which argument can be post-poned into Open() instead of constructor?
        solidfile.Open("solid.bp");


        // Open a file with a Method that has selected a group and a processor in the XML
        // The transport method(s) are (must be) associated with the processor
        // "a" will append to an already existing file, "w" would create a new file
        // Multiple writes to the same file work as append in this application run
        // FIXME: how do we support Update to same step?
        adios::ADIOS_OUTPUT fluidfile( comm, "a", "fluid");
        fluidfile.Open("fluid.bp");

        adios::ADIOS_OUTPUT ckptfile( comm, "w", "checkpoint");
        // we do not open this here, but every time when needed in a function

        // Another output not associated with a single group, so that we can mix variables to it
        //adios:handle vizstream = adios.Open( "stream.bp", comm, "w", "STAGING", "options to staging method");
        adios::handle vizstream = adios.Open( "stream.bp", comm, "w", "groupless");

        // This creates an empty group inside, and we can write all kinds of variables to it

        //Get Monitor info
        std::ofstream logStream( "info_" + std::to_string(rank) + ".log" );
        adios.MonitorGroups( logStream );

        for (int it = 1; it <= 100; it++)
        {
            compute (solid, fluid);

            write_data(solid, solidfile);
            write_data(fluid, fluidfile);

            if (it%10 == 0) {
                write_checkpoint (solid, fluid);
            }

            write_viz(solid, fluid, vizstream);

            MPI_Barrier (comm);
            if (rank==0) printf("Timestep %d written\n", it);
        }

        adios.Close(solidfile);
        adios.Close(fluidfile);
        adios.Close(vizstream);

        // need barrier before we destroy the ADIOS object here automatically
        MPI_Barrier (comm);
        if (rank==0) printf("Finalize adios\n");
    }
    catch( std::exception& e ) //need to think carefully how to handle C++ exceptions with MPI to avoid deadlocking
    {
        if( rank == 0 )
        {
            std::cout << e.what() << "\n";
        }
    }

    delete[] solid.t;
    delete[] fluid.t;

    if (rank==0) printf("Finalize MPI\n");
    MPI_Finalize ();
    return 0;
}
