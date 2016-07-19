// Methods for parsing XML files.

/*
********************************************************************
Copyright Notice
----------------

Building Controls Virtual Test Bed (BCVTB) Copyright (c) 2008, The
Regents of the University of California, through Lawrence Berkeley
National Laboratory (subject to receipt of any required approvals from
the U.S. Dept. of Energy). All rights reserved.

If you have questions about your rights to use or distribute this
software, please contact Berkeley Lab's Technology Transfer Department
at TTD@lbl.gov

NOTICE.  This software was developed under partial funding from the U.S.
Department of Energy.  As such, the U.S. Government has been granted for
itself and others acting on its behalf a paid-up, nonexclusive,
irrevocable, worldwide license in the Software to reproduce, prepare
derivative works, and perform publicly and display publicly.  Beginning
five (5) years after the date permission to assert copyright is obtained
from the U.S. Department of Energy, and subject to any subsequent five
(5) year renewals, the U.S. Government is granted for itself and others
acting on its behalf a paid-up, nonexclusive, irrevocable, worldwide
license in the Software to reproduce, prepare derivative works,
distribute copies to the public, perform publicly and display publicly,
and to permit others to do so.


Modified BSD License agreement
------------------------------

Building Controls Virtual Test Bed (BCVTB) Copyright (c) 2008, The
Regents of the University of California, through Lawrence Berkeley
National Laboratory (subject to receipt of any required approvals from
the U.S. Dept. of Energy).  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
   3. Neither the name of the University of California, Lawrence
      Berkeley National Laboratory, U.S. Dept. of Energy nor the names
      of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

You are under no obligation whatsoever to provide any bug fixes,
patches, or upgrades to the features, functionality or performance of
the source code ("Enhancements") to anyone; however, if you choose to
make your Enhancements available either publicly, or directly to
Lawrence Berkeley National Laboratory, without imposing a separate
written license agreement for such Enhancements, then you hereby grant
the following license: a non-exclusive, royalty-free perpetual license
to install, use, modify, prepare derivative works, incorporate into
other computer software, distribute, and sublicense such enhancements or
derivative works thereof, in binary and source code form.

********************************************************************
*/

///////////////////////////////////////////////////////////
/// \file    utilXml.h
/// \brief   Methods for getting xml values 
///          using the expat libray
///
/// \author  Rui Zhang
///          Carnegie Mellon University
///          ruiz@cmu.edu
/// \date    2009-08-11
///
/// \version $Id: utilXml.h 55724 2009-09-16 17:51:58Z mwetter $
/// 
/// This file provides methods to get general xml values \c getxmlvalue
/// using simple xpath expressions
/// values will be in the same order as they are in the xml file
///
/// This file also provides methods to get the EnergyPlus \c getepvariables.
/// The variables returned will be in the same order as they are in the 
/// configuration file.
/// \sa getxmlvalue()
/// \sa getepvariables()
///
//////////////////////////////////////////////////////////

#include <stdio.h>
//#include <stdlib.h>
#include <string.h>
#include <expat.h>
//#include <errno.h>

#if defined(__amigaos__) && defined(__USE_INLINE__)
#include <proto/expat.h>
#endif

#ifdef XML_LARGE_SIZE
#if defined(XML_USE_MSC_EXTENSIONS) && _MSC_VER < 1400
#define XML_FMT_INT_MOD "I64"
#else
#define XML_FMT_INT_MOD "ll"
#endif
#else
#define XML_FMT_INT_MOD "l"
#endif


////////////////////////////////////////////////////////////////
/// Stack operation, this function will pop one element from stack
/// and will free the resource unused
////////////////////////////////////////////////////////////////
int
stackPopBCVTB();

////////////////////////////////////////////////////////////////
/// Stack operation, will push one element into the stack
/// and will allocate memory for the new element, hence is deep copy
////////////////////////////////////////////////////////////////
int
stackPushBCVTB(char const * str);

////////////////////////////////////////////////////////////////
/// This is a general function that returns the value according to \c exp
///
/// \c exp mimics the xPath expression.
/// Its format is //el1/../eln[@attr]
/// which will return the \c attr value of \c eln, 
/// where \c eln is the n-th child of \c el1
///
/// Example: //variable/EnergyPlus[@name] will return the name attributes of EnergyPlus
/// which is equivalent to //EnergyPlus[@name]
///
///\param fileName the xml file name.  
///\param exp the xPath expression.
///\param myVals string to store the found values, semicolon separated.
///\param mynumVals number of values found.
///\param myStrLen length of the string that is passed.
////////////////////////////////////////////////////////////////
int 
getxmlvalues(
 char const * const fileName, 
 char const * const exp, 
 char * const myVals, 
 int * const myNumVals,
 int const myStrLen
);

////////////////////////////////////////////////////////////////
/// This method returns the number of xmlvalues given xPath expressions.
/// This method will call the function \c getxmlvalues
///
/// \c exp mimics the xPath expression.
/// Its format is //el1/../eln[@attr]
/// which will return the \c attr value of \c eln, 
/// where \c eln is the n-th child of \c el1
///
/// Example: //variable/EnergyPlus[@name] will return the name attributes of EnergyPlus
/// which is equivalent to //EnergyPlus[@name]
///
///\param fileName the name of the xml file
///\param exp the xPath expression
////////////////////////////////////////////////////////////////
int
getnumberofxmlvalues(
 char const * const fileName,
 char const * const exp
);

////////////////////////////////////////////////////////////////
/// This method returns one xmlvalue for a given xPath expressions.
/// The function will call the function \c getxmlvalues to get the variables
/// without ";" at the end of the parsed string
///
/// Return values: 0 normal; -1 error 
///
/// \c exp mimics the xPath expression.
/// Its format is //el1/../eln[@attr]
/// which will return the \c attr value of \c eln, 
/// where \c eln is the n-th child of \c el1
///
/// Example: //variable/EnergyPlus[@name] will return the name attributes of EnergyPlus
/// which is equivalent to //EnergyPlus[@name]
///
///\param fileName the xml file name.  
///\param exp the xPath expression.
///\param str string to store the found values, semicolon separated.
///\param nVals number of values found.
///\param strLen the string length allocated.
////////////////////////////////////////////////////////////////
int
getxmlvalue(
 char const * const fileName,
 char const * const exp,
 char * const str,
 int * const nVals,
 int const strLen
);
