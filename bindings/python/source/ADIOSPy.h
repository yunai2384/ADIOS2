/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * ADIOSPy.h  python binding to ADIOS class
 *
 *  Created on: Mar 13, 2017
 *      Author: William F Godoy godoywf@ornl.gov
 */

#ifndef BINDINGS_PYTHON_SOURCE_ADIOSPY_H_
#define BINDINGS_PYTHON_SOURCE_ADIOSPY_H_

/// \cond EXCLUDE_FROM_DOXYGEN
#include <string>
/// \endcond

#include "IOPy.h"
#include "adios2/ADIOSMPICommOnly.h"
#include "adios2/core/ADIOS.h"

namespace adios
{

class ADIOSPy
{

public:
    ADIOSPy(MPI_Comm mpiComm, const bool debug = false);
    ~ADIOSPy() = default;

    /** testing mpi4py should go away*/
    void HelloMPI();

    IOPy &DeclareIO(const std::string methodName);

private:
    const bool m_DebugMode;
    adios::ADIOS m_ADIOS;
};

} // end namespace adios

#endif /* BINDINGS_PYTHON_SOURCE_ADIOSPY_H_ */
