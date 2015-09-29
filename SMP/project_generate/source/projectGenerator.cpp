/*
 * copyright (c) 2014 Matthew Oliver
 *
 * This file is part of ShiftMediaProject.
 *
 * ShiftMediaProject is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ShiftMediaProject is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ShiftMediaProject; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "projectGenerator.h"

#include <algorithm>
#include <set>
#include <Windows.h>
#include <process.h>
#include <direct.h>

bool projectGenerator::passAllMake( )
{
    //Loop through each library make file
    vector<string> vLibraries;
    m_ConfigHelper.getConfigList( "LIBRARY_LIST", vLibraries );
    for( vector<string>::iterator vitLib=vLibraries.begin(); vitLib<vLibraries.end(); vitLib++ )
    {
        //Check if library is enabled
        if( m_ConfigHelper.getConfigOption( *vitLib )->m_sValue.compare("1") == 0 )
        {
            m_sProjectDir = "..\\..\\..\\lib" + *vitLib + "\\";
            //Locate the project dir for specified library
            string sRetFileName;
            if( !findFile( m_sProjectDir + "MakeFile", sRetFileName ) )
            {
                cout << "  Error: Could not locate directory for library (" << *vitLib << ")" << endl;
                return false;
            }
            //Run passMake on default Makefile
            if( !passMake( ) )
            {
                return false;
            }
            //Check for any sub directories
            m_sProjectDir += "x86\\";
            if( findFile( m_sProjectDir + "MakeFile", sRetFileName ) )
            {
                //Pass the sub directory
                if( !passMake( ) )
                {
                    return false;
                }
            }
            //Reset project dir so it does not include additions
            m_sProjectDir.resize( m_sProjectDir.length() - 4 );
            //Output the project
            if( !outputProject( ) )
            {
                return false;
            }
            //Reset all internal values
            m_sInLine.clear( );
            m_vIncludes.clear( );
            m_vCPPIncludes.clear( );
            m_vCIncludes.clear( );
            m_vYASMIncludes.clear( );
            m_vHIncludes.clear( );
            m_vLibs.clear( );
            m_mUnknowns.clear( );
            m_sProjectDir.clear( );
        }
    }

    //Output the solution file
    return outputSolution( );
}

bool projectGenerator::outputProject( )
{
    //Output the generated files
    uint uiSPos = m_sProjectDir.rfind( '\\', m_sProjectDir.length( ) - 2 ) + 1;
    string sProjectName = m_sProjectDir.substr( uiSPos, m_sProjectDir.length( ) - 1 - uiSPos );
    //Check that all headers are correct
    for( StaticList::iterator itIt=m_vHIncludes.begin( ); itIt!=m_vHIncludes.end( ); itIt++ )
    {
        string sRetFileName;
        if( !findSourceFile( *itIt, ".h", sRetFileName ) )
        {
            cout << "  Error: could not find input header file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ..\..\ so the file is up 2 directories
    }

    //Check that all C Source are correct
    for( StaticList::iterator itIt=m_vCIncludes.begin( ); itIt!=m_vCIncludes.end( ); itIt++ )
    {
        string sRetFileName;
        if( !findSourceFile( *itIt, ".c", sRetFileName ) )
        {
            cout << "  Error: could not find input C source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ..\..\ so the file is up 2 directories
    }

    //Check that all CPP Source are correct
    for( StaticList::iterator itIt=m_vCPPIncludes.begin( ); itIt!=m_vCPPIncludes.end( ); itIt++ )
    {
        string sRetFileName;
        if( !findSourceFile( *itIt, ".cpp", sRetFileName ) )
        {
            cout << "  Error: could not find input C++ source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ..\..\ so the file is up 2 directories
    }

    //Check that all ASM Source are correct
    for( StaticList::iterator itIt=m_vYASMIncludes.begin( ); itIt!=m_vYASMIncludes.end( ); itIt++ )
    {
        string sRetFileName;
        if( !findSourceFile( *itIt, ".asm", sRetFileName ) )
        {
            cout << "  Error: could not find input ASM source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ..\..\ so the file is up 2 directories
    }

    //Check the output Unknown Includes and find there corresponding file
    for( StaticList::iterator itIt=m_vIncludes.begin( ); itIt!=m_vIncludes.end( ); itIt++ )
    {
        string sRetFileName;
        if( findSourceFile( *itIt, ".c", sRetFileName ) )
        {
            //Found a C File to include
            if( find( m_vCIncludes.begin(), m_vCIncludes.end(), sRetFileName ) != m_vCIncludes.end() )
            {
                //skip this item
                continue;
            }
            m_vCIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ..\..\ so the file is up 2 directories
        }
        else if( findSourceFile( *itIt, ".cpp", sRetFileName ) )
        {
            //Found a C++ File to include
            if( find( m_vCPPIncludes.begin(), m_vCPPIncludes.end(), sRetFileName ) != m_vCPPIncludes.end() )
            {
                //skip this item
                continue;
            }
            m_vCPPIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ..\..\ so the file is up 2 directories
        }
        else if( findSourceFile( *itIt, ".asm", sRetFileName ) )
        {
            //Found a C++ File to include
            if( find( m_vYASMIncludes.begin(), m_vYASMIncludes.end(), sRetFileName ) != m_vYASMIncludes.end() )
            {
                //skip this item
                continue;
            }
            m_vYASMIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ..\..\ so the file is up 2 directories
        }
        else
        {
            cout << "  Error: Could not find valid source file for object (" << *itIt << ")" << endl;
            return false;
        }
    }

    //We now have complete list of all the files the we need
    cout << "  Generating project file (" << sProjectName << ")..." << endl;
    //Open the input temp project file
    m_ifInputFile.open( "../templates/template_in.vcxproj" );
    if( !m_ifInputFile.is_open( ) )
    {
        cout << "  Error: Failed opening template project (../templates/template_in.vcxproj)" << endl;
        return false;
    }

    //Load whole file into internal string
    string sProjectFile;
    m_ifInputFile.seekg( 0, m_ifInputFile.end );
    uint uiBufferSize = (uint)m_ifInputFile.tellg();
    m_ifInputFile.seekg( 0, m_ifInputFile.beg );
    sProjectFile.resize( uiBufferSize );
    m_ifInputFile.read( &sProjectFile[0], uiBufferSize );
    if( uiBufferSize != m_ifInputFile.gcount() )
    {
        sProjectFile.resize( (uint)m_ifInputFile.gcount() );
    }
    m_ifInputFile.close( );

    //Open the input temp project file filters
    m_ifInputFile.open( "../templates/template_in.vcxproj.filters" );
    if( !m_ifInputFile.is_open( ) )
    {
        cout << "  Error: Failed opening template project (../templates/template_in.vcxproj.filters)" << endl;
        return false;
    }

    //Load whole file into internal string
    string sFiltersFile;
    m_ifInputFile.seekg( 0, m_ifInputFile.end );
    uiBufferSize = (uint)m_ifInputFile.tellg();
    m_ifInputFile.seekg( 0, m_ifInputFile.beg );
    sFiltersFile.resize( uiBufferSize );
    m_ifInputFile.read( &sFiltersFile[0], uiBufferSize );
    if( uiBufferSize != m_ifInputFile.gcount() )
    {
        sFiltersFile.resize( (uint)m_ifInputFile.gcount() );
    }
    m_ifInputFile.close( );

    //Change all occurance of template_in with project name
    const string sFFSearchTag = "template_in";
    uint uiFindPos = sProjectFile.find( sFFSearchTag );
    while( uiFindPos != string::npos )
    {
        //Replace
        sProjectFile.replace( uiFindPos, sFFSearchTag.length( ), sProjectName );
        //Get next
        uiFindPos = sProjectFile.find( sFFSearchTag, uiFindPos + 1 );
    }
    uint uiFindPosFilt = sFiltersFile.find( sFFSearchTag );
    while( uiFindPosFilt != string::npos )
    {
        //Replace
        sFiltersFile.replace( uiFindPosFilt, sFFSearchTag.length( ), sProjectName );
        //Get next
        uiFindPosFilt = sFiltersFile.find( sFFSearchTag, uiFindPosFilt + 1 );
    }

    //Change all occurance of template_shin with short project project name
    const string sFFShortSearchTag = "template_shin";
    uiFindPos = sProjectFile.find( sFFShortSearchTag );
    string sProjectNameShort = sProjectName.substr( 3 ); //The full name minus the lib prefix
    while( uiFindPos != string::npos )
    {
        //Replace
        sProjectFile.replace( uiFindPos, sFFShortSearchTag.length( ), sProjectNameShort );
        //Get next
        uiFindPos = sProjectFile.find( sFFShortSearchTag, uiFindPos + 1 );
    }
    uiFindPosFilt = sFiltersFile.find( sFFShortSearchTag );
    while( uiFindPosFilt != string::npos )
    {
        //Replace
        sFiltersFile.replace( uiFindPosFilt, sFFShortSearchTag.length( ), sProjectNameShort );
        //Get next
        uiFindPosFilt = sFiltersFile.find( sFFShortSearchTag, uiFindPosFilt + 1 );
    }

    //Change all occurance of template_platform with specified project toolchain
    string sToolchain;
    if( !passToolchain( sToolchain ) )
    {
        return false;
    }
    //If toolchain is newer than 2013 then add additional toolsets
    if( sToolchain.compare( "v150" ) == 0 )
    {
        sToolchain = "v120</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='14.0'\">v140</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='15.0'\">v150";
    }
    else if( sToolchain.compare( "v140" ) == 0 )
    {
        sToolchain = "v120</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='14.0'\">v140";
    }
    const string sPlatformSearch = "template_platform";
    uiFindPos = sProjectFile.find( sPlatformSearch );
    while( uiFindPos != string::npos )
    {
        //Replace
        sProjectFile.replace( uiFindPos, sPlatformSearch.length( ), sToolchain );
        //Get next
        uiFindPos = sProjectFile.find( sPlatformSearch, uiFindPos + 1 );
    }

    //Set the project key
    string sGUID = "<ProjectGuid>{";
    uiFindPos = sProjectFile.find( sGUID );
    if( uiFindPos != string::npos )
    {
        map<string, string> mLibKeys;
        map<string, string> mProgramKeys;
        buildProjectGUIDs( mLibKeys, mProgramKeys );
        uiFindPos += sGUID.length( );
        sProjectFile.replace( uiFindPos, mLibKeys[sProjectName].length( ), mLibKeys[sProjectName] );
    }

    //After </ItemGroup> add the item groups for each of the include types
    string sItemGroup = "\n  <ItemGroup>";
    string sItemGroupEnd = "\n  </ItemGroup>";
    uiFindPos = sProjectFile.find( sItemGroupEnd );
    uiFindPos += sItemGroupEnd.length( );
    uiFindPosFilt = sFiltersFile.find( sItemGroupEnd );
    uiFindPosFilt += sItemGroupEnd.length( );
    string sCLCompile = "\n    <ClCompile Include=\"";
    string sCLCompileClose = "\">";
    string sCLCompileObject = "\n      <ObjectFileName>$(IntDir)\\";
    string sCLCompileObjectClose = ".obj</ObjectFileName>";
    string sCLCompileEnd = "\n    </ClCompile>";
    string sFilterSource = "\n      <Filter>Source Files";
    string sSource = "Source Files";
    string sFilterEnd = "</Filter>";
    set<string> vSubFilters;
    StaticList vFoundObjects;

    //Output ASM files in specific item group (must go first as asm does not allow for custom obj filename)
    if( m_vYASMIncludes.size( ) > 0 )
    {
        if( m_ConfigHelper.getConfigOptionPrefixed( "HAVE_YASM" )->m_sValue.compare( "1" ) == 0 )
        {
            string sYASMInclude = "\n    <YASM Include=\"";
            string sYASMIncludeEnd = "\" />";
            string sYASMIncludeEndFilt = "\">";
            string sYASMIncludeClose = "\n    </YASM>";
            string sYASMFiles = sItemGroup;
            string sYASMFilesFilt = sItemGroup;
            for( StaticList::iterator vitInclude = m_vYASMIncludes.begin( ); vitInclude < m_vYASMIncludes.end( ); vitInclude++ )
            {
                //Output YASM objects
                sYASMFiles += sYASMInclude;
                sYASMFilesFilt += sYASMInclude;
                //Add the fileName
                replace( vitInclude->begin( ), vitInclude->end( ), '/', '\\' );
                uint uiPos = vitInclude->rfind( '\\' ) + 1;
                string sObjectName = vitInclude->substr( uiPos );
                uint uiPos2 = sObjectName.rfind( '.' );
                sObjectName.resize( uiPos2 );
                vFoundObjects.push_back( sObjectName );
                sYASMFiles += *vitInclude;
                sYASMFilesFilt += *vitInclude;
                //Close the current item
                sYASMFiles += sYASMIncludeEnd;
                sYASMFilesFilt += sYASMIncludeEndFilt;
                //Add the filters Filter
                sYASMFilesFilt += sFilterSource;
                uiPos = vitInclude->rfind( "..\\" );
                uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
                string sFolderName = vitInclude->substr( uiPos, vitInclude->rfind( '\\' ) - uiPos );
                if( sFolderName.length( ) > 0 )
                {
                    sFolderName = "\\" + sFolderName;
                    vSubFilters.insert( sSource + sFolderName );
                    sYASMFilesFilt += sFolderName;
                }
                sYASMFilesFilt += sFilterEnd;
                sYASMFilesFilt += sYASMIncludeClose;
            }
            sYASMFiles += sItemGroupEnd;
            sYASMFilesFilt += sItemGroupEnd;
            //Insert into output file
            sProjectFile.insert( uiFindPos, sYASMFiles );
            uiFindPos += sYASMFiles.length( );
            sFiltersFile.insert( uiFindPosFilt, sYASMFilesFilt );
            uiFindPosFilt += sYASMFilesFilt.length( );
        }
    }

    //Output C files
    if( m_vCIncludes.size() > 0 )
    {
        string sCFiles = sItemGroup;
        string sCFilesFilt = sItemGroup;
        for( StaticList::iterator vitInclude=m_vCIncludes.begin(); vitInclude<m_vCIncludes.end(); vitInclude++ )
        {
            //Output CLCompile objects
            sCFiles += sCLCompile;
            sCFilesFilt += sCLCompile;
            //Add the fileName
            replace( vitInclude->begin( ), vitInclude->end( ), '/', '\\' );
            sCFiles += *vitInclude;
            sCFilesFilt += *vitInclude;
            sCFiles += sCLCompileClose;
            sCFilesFilt += sCLCompileClose;
            //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
            uint uiPos = vitInclude->rfind( '\\' ) + 1;
            string sObjectName = vitInclude->substr( uiPos );
            uint uiPos2 = sObjectName.rfind( '.' );
            sObjectName.resize( uiPos2 );
            string sPath = vitInclude->substr( 0, uiPos );
            uiPos = vitInclude->rfind( "..\\" );
            uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
            if( find( vFoundObjects.begin( ), vFoundObjects.end( ), sObjectName ) != vFoundObjects.end() )
            {
                sObjectName = vitInclude->substr( uiPos );
                replace( sObjectName.begin( ), sObjectName.end( ), '\\', '_' );
                //Replace the extension with obj
                uiPos2 = sObjectName.rfind( '.' );
                sObjectName.resize( uiPos2 );
                sCFiles += sCLCompileObject;
                sCFiles += sObjectName;
                sCFiles += sCLCompileObjectClose;
            }
            else
            {
                vFoundObjects.push_back( sObjectName );
            }
            //Add the filters Filter
            sCFilesFilt += sFilterSource;
            string sFolderName = vitInclude->substr( uiPos, vitInclude->rfind( '\\' ) - uiPos );
            if( sFolderName.length() > 0 )
            {
                sFolderName = "\\" + sFolderName;
                vSubFilters.insert( sSource + sFolderName );
                sCFilesFilt += sFolderName;
            }
            sCFilesFilt += sFilterEnd;
            //Close the current item
            sCFiles += sCLCompileEnd;
            sCFilesFilt += sCLCompileEnd;
        }
        sCFiles += sItemGroupEnd;
        sCFilesFilt += sItemGroupEnd;
        //Insert into output file
        sProjectFile.insert( uiFindPos, sCFiles );
        uiFindPos += sCFiles.length( );
        sFiltersFile.insert( uiFindPosFilt, sCFilesFilt );
        uiFindPosFilt += sCFilesFilt.length( );
    }

    //Also output C++ files in same item group
    if( m_vCPPIncludes.size() > 0 )
    {
        string sCPPFiles = sItemGroup;
        string sCPPFilesFilt = sItemGroup;
        for( StaticList::iterator vitInclude=m_vCPPIncludes.begin(); vitInclude<m_vCPPIncludes.end(); vitInclude++ )
        {
            //Output CLCompile objects
            sCPPFiles += sCLCompile;
            sCPPFilesFilt += sCLCompile;
            //Add the fileName
            replace( vitInclude->begin( ), vitInclude->end( ), '/', '\\' );
            sCPPFiles += *vitInclude;
            sCPPFilesFilt += *vitInclude;
            sCPPFiles += sCLCompileClose;
            sCPPFilesFilt += sCLCompileClose;
            //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
            uint uiPos = vitInclude->rfind( '\\' ) + 1;
            string sObjectName = vitInclude->substr( uiPos );
            uint uiPos2 = sObjectName.rfind( '.' );
            sObjectName.resize( uiPos2 );
            uiPos = vitInclude->rfind( '\\' );
            string sPath = vitInclude->substr( 0, uiPos + 1 );
            uiPos = vitInclude->rfind( "..\\" );
            uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
            if( find( vFoundObjects.begin( ), vFoundObjects.end( ), sObjectName ) != vFoundObjects.end( ) )
            {
                sObjectName = vitInclude->substr( uiPos );
                replace( sObjectName.begin( ), sObjectName.end( ), '\\', '_' );
                //Replace the extension with obj
                uiPos2 = sObjectName.rfind( '.' );
                sObjectName.resize( uiPos2 );
                sCPPFiles += sCLCompileObject;
                sCPPFiles += sObjectName;
                sCPPFiles += sCLCompileObjectClose;
            }
            else
            {
                vFoundObjects.push_back( sObjectName );
            }
            //Add the filters Filter
            sCPPFilesFilt += sFilterSource;
            string sFolderName = vitInclude->substr( uiPos, vitInclude->rfind( '\\' ) - uiPos );
            if( sFolderName.length() > 0 )
            {
                sFolderName = "\\" + sFolderName;
                vSubFilters.insert( sSource + sFolderName );
                sCPPFilesFilt += sFolderName;
            }
            sCPPFilesFilt += sFilterEnd;
            //Close the current item
            sCPPFiles += sCLCompileEnd;
            sCPPFilesFilt += sCLCompileEnd;
        }
        sCPPFiles += sItemGroupEnd;
        sCPPFilesFilt += sItemGroupEnd;
        //Insert into output file
        sProjectFile.insert( uiFindPos, sCPPFiles );
        uiFindPos += sCPPFiles.length( );
        sFiltersFile.insert( uiFindPosFilt, sCPPFilesFilt );
        uiFindPosFilt += sCPPFilesFilt.length( );
    }

    //Output header files in new item group
    if( m_vHIncludes.size() > 0 )
    {
        string sCLInclude = "\n    <ClInclude Include=\"";
        string sCLIncludeEnd = "\" />";
        string sCLIncludeEndFilt = "\">";
        string sCLIncludeClose = "\n    </ClInclude>";
        string sFilterHeader = "\n      <Filter>Header Files";
        string sHeaders = "Header Files";
        string sHFiles = sItemGroup;
        string sHFilesFilt = sItemGroup;
        for( StaticList::iterator vitInclude=m_vHIncludes.begin(); vitInclude<m_vHIncludes.end(); vitInclude++ )
        {
            //Output CLInclude objects
            sHFiles += sCLInclude;
            sHFilesFilt += sCLInclude;
            //Add the fileName
            replace( vitInclude->begin( ), vitInclude->end( ), '/', '\\' );
            sHFiles += *vitInclude;
            sHFilesFilt += *vitInclude;
            //Close the current item
            sHFiles += sCLIncludeEnd;
            sHFilesFilt += sCLIncludeEndFilt;
            //Add the filters Filter
            sHFilesFilt += sFilterHeader;
            uint uiPos = vitInclude->rfind( "..\\" );
            uiPos = (uiPos == string::npos)? 0 : uiPos+3;
            string sFolderName = vitInclude->substr( uiPos, vitInclude->rfind( '\\' ) - uiPos );
            if( sFolderName.length() > 0 )
            {
                sFolderName = "\\" + sFolderName;
                vSubFilters.insert( sHeaders + sFolderName );
                sHFilesFilt += sFolderName;
            }
            sHFilesFilt += sFilterEnd;
            sHFilesFilt += sCLIncludeClose;
        }
        sHFiles += sItemGroupEnd;
        sHFilesFilt += sItemGroupEnd;
        //Insert into output file
        sProjectFile.insert( uiFindPos, sHFiles );
        uiFindPos += sHFiles.length( );
        sFiltersFile.insert( uiFindPosFilt, sHFilesFilt );
        uiFindPosFilt += sHFilesFilt.length( );
    }

    //After </Lib> and </Link> and the post and then pre build events
    string asLibLink[2] = { "</Lib>", "</Link>" };
    string sYASMDefines = "\n\
    <YASM>\n\
      <IncludePaths>..\\;.\\;..\\libavcodec;%(IncludePaths)</IncludePaths>\n\
      <PreIncludeFile>config.asm</PreIncludeFile>\n\
      <Debug>true</Debug>\n\
    </YASM>";
    string sPostbuild = "\n    <PostBuildEvent>\n\
      <Command>";
    string sPostbuildClose = "</Command>\n\
    </PostBuildEvent>";
    string sInclude = "mkdir $(OutDir)\\include\n\
mkdir $(OutDir)\\include\\";
    string sCopy = "\ncopy ";
    string sCopyEnd = " $(OutDir)\\include\\";
    string sLicense = "\nmkdir $(OutDir)\\licenses";
    string sLicenseName = m_ConfigHelper.m_sProjectName;
    transform( sLicenseName.begin( ), sLicenseName.end( ), sLicenseName.begin( ), ::tolower );
    string sLicenseEnd = " $(OutDir)\\licenses\\" + sLicenseName + ".txt";
    string sPrebuild = "\n    <PreBuildEvent>\n\
      <Command>if exist ..\\config.h (\n\
del ..\\config.h\n\
)\n\
if exist ..\\version.h (\n\
del ..\\version.h\n\
)\n\
if exist ..\\config.asm (\n\
del ..\\config.asm\n\
)\n\
if exist ..\\libavutil\\avconfig.h (\n\
del ..\\libavutil\\avconfig.h\n\
)\n\
if exist ..\\libavutil\\ffversion.h (\n\
del ..\\libavutil\\ffversion.h\n\
)\n\
if exist $(OutDir)\\include\\" + sProjectName + " (\n\
rd /s /q $(OutDir)\\include\\" + sProjectName + "\n\
cd ../\n\
cd $(ProjectDir)\n\
)</Command>\n\
    </PreBuildEvent>";
    //Get the correct license file
    string sLicenseFile;
    if( m_ConfigHelper.getConfigOption( "nonfree" )->m_sValue.compare("1") == 0 )
    {
        sLicenseFile = "..\\COPYING.GPLv3"; //Technically this has no license as it is unredistributable but we get the closest thing for now
    }
    else if( m_ConfigHelper.getConfigOption( "gplv3" )->m_sValue.compare("1") == 0 )
    {
        sLicenseFile = "..\\COPYING.GPLv3";
    }
    else if( m_ConfigHelper.getConfigOption( "lgplv3" )->m_sValue.compare("1") == 0 )
    {
        sLicenseFile = "..\\COPYING.LGPLv3";
    }
    else if( m_ConfigHelper.getConfigOption( "gpl" )->m_sValue.compare("1") == 0 )
    {
        sLicenseFile = "..\\COPYING.GPLv2";
    }
    else
    {
        sLicenseFile = "..\\COPYING.LGPLv2.1";
    }
    //Generate the pre build and post build string
    string sAdditional;
    //Add the post build event
    sAdditional += sPostbuild;
    if( m_vHIncludes.size( ) > 0 )
    {
        sAdditional += sInclude;
        sAdditional += sProjectName;
        for( StaticList::iterator vitHeaders=m_vHIncludes.begin(); vitHeaders<m_vHIncludes.end(); vitHeaders++ )
        {
            sAdditional += sCopy;
            replace( vitHeaders->begin(), vitHeaders->end(), '/', '\\' );
            sAdditional += *vitHeaders;
            sAdditional += sCopyEnd;
            sAdditional += sProjectName;
        }
    }
    //Output license
    sAdditional += sLicense;
    sAdditional += sCopy;
    sAdditional += sLicenseFile;
    sAdditional += sLicenseEnd;
    sAdditional += sPostbuildClose;
    //Add the pre build event
    sAdditional += sPrebuild;
    //Also after above add the YASM settings
    if( m_ConfigHelper.getConfigOptionPrefixed("HAVE_YASM")->m_sValue.compare("1") == 0 )
    {
        sAdditional += sYASMDefines;
    }

    for( uint uiI=0; uiI<2; uiI++ )
    {
        uiFindPos = sProjectFile.find( asLibLink[uiI] );
        while( uiFindPos != string::npos )
        {
            uiFindPos += asLibLink[uiI].length( );
            //Add to output
            sProjectFile.insert( uiFindPos, sAdditional );
            uiFindPos += sAdditional.length( );
            //Get next
            uiFindPos = sProjectFile.find( asLibLink[uiI], uiFindPos+1 );
        }
    }

    if( ( m_ConfigHelper.getConfigOptionPrefixed("HAVE_YASM")->m_sValue.compare("1") == 0 ) && ( m_vYASMIncludes.size() > 0 ) )
    {
        string sFindProps = "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />";
        uiFindPos = sProjectFile.find( sFindProps ) + sFindProps.length();
        //After <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" /> add yasm props
        string sYASMProps = "\n\
  <ImportGroup Label=\"ExtensionSettings\">\n\
    <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\vsyasm.props\" />\n\
  </ImportGroup>";
        sProjectFile.insert( uiFindPos, sYASMProps );
        uiFindPos += sYASMProps.length( );

        string sFindTargets = "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />";
        uiFindPos = sProjectFile.find( sFindTargets ) + sFindTargets.length();
        //After <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" /> add yasm target
        string sYASMTargets = "\n\
  <ImportGroup Label=\"ExtensionTargets\">\n\
    <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\vsyasm.targets\" />\n\
  </ImportGroup>";
        sProjectFile.insert( uiFindPos, sYASMTargets );
        uiFindPos += sYASMTargets.length( );
    }

    //Check current libs list for valid lib names
    for( StaticList::iterator vitLib=m_vLibs.begin(); vitLib<m_vLibs.end(); vitLib++ )
    {
        //prepend lib if needed
        if( vitLib->find("lib") != 0 )
        {
            *vitLib = "lib" + *vitLib;
        }
    }

    //Add additional dependencies based on current config to Libs list
    buildInterDependencies( sProjectName, m_vLibs );
    m_mProjectLibs[sProjectName] = m_vLibs; //Backup up current libs for solution
    StaticList vAddLibs;
    StaticList vIncludeDirs;
    StaticList vLib32Dirs;
    StaticList vLib64Dirs;
    buildDependencies( sProjectName, m_vLibs, vAddLibs, vIncludeDirs, vLib32Dirs, vLib64Dirs );
    vector<string> vLibraries;
    m_ConfigHelper.getConfigList( "LIBRARY_LIST", vLibraries );
    if( ( m_vLibs.size( ) > 0 ) || ( vAddLibs.size( ) > 0 ) )
    {
        //Add to Additional Dependencies
        string asLibLink2[2] = { "<Lib>", "<Link>" };
        for( uint uiI=0; uiI<2; uiI++ )
        {
            //The additional dependency should be within the Link/Lib section
            string sAdditional[2]; //debug, release
            //Add each dependency to the list
            for( StaticList::iterator vitLib=m_vLibs.begin(); vitLib<m_vLibs.end(); vitLib++ )
            {
                if( uiI == 0 )
                {
                    //If the dependency is actually for one of input files then we can ignore it in static mode
                    //  as this just causes unnecessary code bloat
                    vector<string>::iterator vitLib2=vLibraries.begin();
                    for( vitLib2; vitLib2<vLibraries.end(); vitLib2++ )
                    {
                        if( vitLib->compare("lib"+*vitLib2) == 0 )
                        {
                            break;
                        }
                    }
                    if( vitLib2 != vLibraries.end() )
                    {
                        continue;
                    }
                }

                sAdditional[0] += *vitLib;
                sAdditional[0] += "d.lib;";
                sAdditional[1] += *vitLib;
                sAdditional[1] += ".lib;";
            }
            //Add each additional lib to the list
            for( StaticList::iterator vitLib=vAddLibs.begin(); vitLib<vAddLibs.end(); vitLib++ )
            {
                sAdditional[0] += *vitLib;
                sAdditional[0] += ".lib;";
                sAdditional[1] += *vitLib;
                sAdditional[1] += ".lib;";
            }
            uiFindPos = sProjectFile.find( asLibLink2[uiI] );
            //loop over each debug/release sequence
            for( uint uiJ = 0; uiJ < 2; uiJ++ )
            {
                uint uiMax = ( ( uiJ == 0 ) && ( uiI == 0 ) )? 2 : 4; //No LTO option in debug
                for( uint uiK = 0; uiK < uiMax; uiK++ )
                {
                    uiFindPos = sProjectFile.find( "%(AdditionalDependencies)", uiFindPos );
                    if( uiFindPos == string::npos )
                    {
                        cout << "  Error: Failed finding dependencies in template." << endl;
                        return false;
                    }
                    //Add to output
                    sProjectFile.insert( uiFindPos, sAdditional[uiJ] );
                    uiFindPos += sAdditional[uiJ].length( );
                    //Get next
                    uiFindPos = sProjectFile.find( asLibLink2[uiI], uiFindPos + 1 );
                }
            }
        }
    }
    if( vIncludeDirs.size( ) > 0 )
    {
        //Add additional includes to include list based on current config
        string sAddInclude;
        for( StaticList::iterator vitIt = vIncludeDirs.begin( ); vitIt < vIncludeDirs.end( ); vitIt++ )
        {
            sAddInclude += *vitIt + ";";
        }
        string sAddIncludeDir = "<AdditionalIncludeDirectories>";
        uiFindPos = sProjectFile.find( sAddIncludeDir );
        while( uiFindPos != string::npos )
        {
            //Add to output
            uiFindPos += sAddIncludeDir.length( ); //Must be added first so that it is before $(IncludePath) as otherwise there are errors
            sProjectFile.insert( uiFindPos, sAddInclude );
            uiFindPos += sAddInclude.length( );
            //Get next
            uiFindPos = sProjectFile.find( sAddIncludeDir, uiFindPos + 1 );
        }
    }

    if( ( vLib32Dirs.size( ) > 0 ) || ( vLib64Dirs.size( ) > 0 ) )
    {
        //Add additional lib includes to include list based on current config
        string sAddLibs[2];
        for( StaticList::iterator vitIt = vLib32Dirs.begin( ); vitIt < vLib32Dirs.end( ); vitIt++ )
        {
            sAddLibs[0] += *vitIt + ";";
        }
        for( StaticList::iterator vitIt = vLib64Dirs.begin( ); vitIt < vLib64Dirs.end( ); vitIt++ )
        {
            sAddLibs[1] += *vitIt + ";";
        }
        string sAddLibDir = "<AdditionalLibraryDirectories>";
        uint ui32Or64 = 0; //start with 32 (assumes projects are ordered 32 then 64 recursive)
        uiFindPos = sProjectFile.find( sAddLibDir );
        while( uiFindPos != string::npos )
        {
            //Add to output
            uiFindPos += sAddLibDir.length( );
            sProjectFile.insert( uiFindPos, sAddLibs[ui32Or64] );
            uiFindPos += sAddLibs[ui32Or64].length( );
            //Get next
            uiFindPos = sProjectFile.find( sAddLibDir, uiFindPos + 1 );
            ui32Or64 = !ui32Or64;
        }
    }

    //Add any additional Filters to filters file
    set<string>::iterator sitIt=vSubFilters.begin();
    //get start potion in file
    uiFindPosFilt = sFiltersFile.find( sItemGroupEnd );
    string sFilterAdd = "\n    <Filter Include=\"";
    string sFilterAdd2 = "\">\n\
      <UniqueIdentifier>{";
    string sFilterAddClose = "}</UniqueIdentifier>\n\
    </Filter>";
    string asFilterKeys[] = { "cac6df1e-4a60-495c-8daa-5707dc1216ff", "9fee14b2-1b77-463a-bd6b-60efdcf8850f",
        "bf017c32-250d-47da-b7e6-d5a5091cb1e6", "fd9e10e9-18f6-437d-b5d7-17290540c8b8", "f026e68e-ff14-4bf4-8758-6384ac7bcfaf",
        "a2d068fe-f5d5-4b6f-95d4-f15631533341", "8a4a673d-2aba-4d8d-a18e-dab035e5c446", "0dcfb38d-54ca-4ceb-b383-4662f006eca9",
        "57bf1423-fb68-441f-b5c1-f41e6ae5fa9c" };
    uint uiCurrentKey = 0;
    string sAddFilters;
    while( sitIt != vSubFilters.end() )
    {
        sAddFilters += sFilterAdd;
        sAddFilters += *sitIt;
        sAddFilters += sFilterAdd2;
        sAddFilters += asFilterKeys[uiCurrentKey];
        uiCurrentKey++;
        sAddFilters += sFilterAddClose;
        //next
        sitIt++;
    }
    //Add to string
    sFiltersFile.insert( uiFindPosFilt, sAddFilters );

    //Open output project
    string sOutProjectFile = "../../" + sProjectName + ".vcxproj";
    ofstream ofProjectFile( sOutProjectFile );
    if( !ofProjectFile.is_open( ) )
    {
        cout << "  Error: failed opening output project file (" << sOutProjectFile << ")" << endl;
        return false;
    }

    //Output project file and close
    ofProjectFile << sProjectFile;
    ofProjectFile.close( );

    //Open output filters
    string sOutFiltersFile = "../../" + sProjectName + ".vcxproj.filters";
    ofstream ofFiltersFile( sOutFiltersFile );
    if( !ofFiltersFile.is_open( ) )
    {
        cout << "  Error: failed opening output project filters file (" << sOutProjectFile << ")" << endl;
        return false;
    }

    //Output project filter file and close
    ofFiltersFile << sFiltersFile;
    ofFiltersFile.close( );

    //Open the exports files and get export names
    cout << "  Generating project exports file (" << sProjectName << ")..." << endl;
    string sExportList;
    if( !findFile( this->m_sProjectDir + "\\*.v", sExportList ) )
    {
        cout << "  Error: Failed finding project exports (" << sProjectName << ")" << endl;
        return false;
    }
    //Open the input export file
    m_ifInputFile.open( this->m_sProjectDir + sExportList );
    if( !m_ifInputFile.is_open( ) )
    {
        cout << "  Error: Failed opening project exports (" << this->m_sProjectDir + sExportList << ")" << endl;
        return false;
    }

    //Load whole file into internal string
    string sExportsFile;
    m_ifInputFile.seekg( 0, m_ifInputFile.end );
    uiBufferSize = (uint)m_ifInputFile.tellg( );
    m_ifInputFile.seekg( 0, m_ifInputFile.beg );
    sExportsFile.resize( uiBufferSize );
    m_ifInputFile.read( &sExportsFile[0], uiBufferSize );
    if( uiBufferSize != m_ifInputFile.gcount( ) )
    {
        sExportsFile.resize( (uint)m_ifInputFile.gcount( ) );
    }
    m_ifInputFile.close( );

    //Search for start of global tag
    string sGlobal = "global:";
    StaticList vExportStrings;
    uiFindPos = sExportsFile.find( sGlobal );
    if( uiFindPos != string::npos )
    {
        //Remove everything outside the global section
        uiFindPos += sGlobal.length( );
        uint uiFindPos2 = sExportsFile.find( "local:", uiFindPos );
        sExportsFile = sExportsFile.substr( uiFindPos, uiFindPos2 - uiFindPos );

        //Remove any comments
        uiFindPos = sExportsFile.find( '#' );
        while( uiFindPos != string::npos )
        {
            //find end of line
            uiFindPos2 = sExportsFile.find( 10, uiFindPos + 1 ); //10 is line feed
            sExportsFile.erase( uiFindPos, uiFindPos2 - uiFindPos + 1 );
            uiFindPos = sExportsFile.find( '#', uiFindPos+1 );
        }

        //Clean any remaining white space out
        sExportsFile.erase( remove_if( sExportsFile.begin( ), sExportsFile.end( ), ::isspace ), sExportsFile.end( ) );

        //Get any export strings
        uiFindPos = 0;
        uiFindPos2 = sExportsFile.find( ';' );
        while( uiFindPos2 != string::npos )
        {
            vExportStrings.push_back( sExportsFile.substr( uiFindPos, uiFindPos2 - uiFindPos ) );
            uiFindPos = uiFindPos2 + 1;
            uiFindPos2 = sExportsFile.find( ';', uiFindPos );
        }
    }
    else
    {
        cout << "  Error: Failed finding global start in project exports (" << sExportList << ")" << endl;
        return false;
    }

    //Create a test file to read in definitions
    string sOutDir = "../../../../../msvc32/";
    string sCLExtra = "/I\"" + sOutDir + "include/\"";
    for( StaticList::iterator vitIt = vIncludeDirs.begin( ); vitIt < vIncludeDirs.end( ); vitIt++ )
    {
        string sIncludeDir = *vitIt;
        uint uiFindPos2 = sIncludeDir.find( "$(OutDir)" );
        if( uiFindPos2 != string::npos )
        {
            sIncludeDir.replace( uiFindPos2, 9, sOutDir );
        }
        replace( sIncludeDir.begin( ), sIncludeDir.end( ), '\\', '/' );
        uiFindPos2 = sIncludeDir.find( "$(" );
        if( uiFindPos2 != string::npos )
        {
            sIncludeDir.replace( uiFindPos2, 2, "%" );
        }
        uiFindPos2 = sIncludeDir.find( ")" );
        if( uiFindPos2 != string::npos )
        {
            sIncludeDir.replace( uiFindPos2, 1, "%" );
        }
        sCLExtra += " /I\"" + sIncludeDir + '\"';
    }

    //Split each source file into different directories to avoid name clashes
    map<string,StaticList> mDirectoryObjects;
    for( StaticList::iterator itI = m_vCIncludes.begin( ); itI < m_vCIncludes.end( ); itI++ )
    {
        //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
        uint uiPos = itI->rfind( "..\\" );
        uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
        uint uiPos2 = itI->rfind( '\\' );
        uiPos2 = ( uiPos2 == string::npos ) ? string::npos : uiPos2 - uiPos;
        string sFolderName = itI->substr( uiPos, uiPos2 );
        mDirectoryObjects[sFolderName].push_back( *itI );
    }
    for( StaticList::iterator itI = m_vCPPIncludes.begin( ); itI < m_vCPPIncludes.end( ); itI++ )
    {
        //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
        uint uiPos = itI->rfind( "..\\" );
        uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
        uint uiPos2 = itI->rfind( '\\' );
        uiPos2 = ( uiPos2 == string::npos ) ? string::npos : uiPos2 - uiPos;
        string sFolderName = itI->substr( uiPos, uiPos2 );
        mDirectoryObjects[sFolderName].push_back( *itI );
    }

    //Use Microsoft compiler to pass the test file and retrieve declarations
    string sCLLaunchBat = "@echo off \n";
    sCLLaunchBat += "if exist \"%VS150COMNTOOLS%\\vsvars32.bat\" ( \n\
call \"%VS150COMNTOOLS%\\vsvars32.bat\" \n\
goto MSVCVarsDone \n\
) else if exist \"%VS140COMNTOOLS%\\vsvars32.bat\" ( \n\
call \"%VS140COMNTOOLS%\\vsvars32.bat\" \n\
goto MSVCVarsDone \n\
) else if exist \"%VS120COMNTOOLS%\\vsvars32.bat\" ( \n\
call \"%VS120COMNTOOLS%\\vsvars32.bat\" \n\
goto MSVCVarsDone \n\
) else if exist \"%VS110COMNTOOLS%\\vsvars32.bat\" ( \n\
call \"%VS110COMNTOOLS%\\vsvars32.bat\" \n\
goto MSVCVarsDone \n\
) else ( \n\
exit /b 1 \n\
) \n\
:MSVCVarsDone \n";
    sCLLaunchBat += "mkdir " + sProjectNameShort + " > nul 2>&1\n";
    for( map<string, StaticList>::iterator itI = mDirectoryObjects.begin( ); itI != mDirectoryObjects.end( ); itI++ )
    {
        //Need to make output directory so cl doesnt fail outputting objs
        string sDirName = sProjectNameShort + "\\" + itI->first;
        sCLLaunchBat += "mkdir " + sDirName + " > nul 2>&1\n";
        const uint uiRowSize = 32;
        uint uiNumCLCalls = (uint)ceilf( (float)itI->second.size( ) / (float)uiRowSize );
        uint uiTotalPos = 0;
        //Split calls into groups of 50 to prevent batch file length limit
        for( uint uiI = 0; uiI < uiNumCLCalls; uiI++ )
        {
            sCLLaunchBat += "cl.exe";
            sCLLaunchBat += " /I\"../../\" /I\"../../../\" " + sCLExtra + " /Fo\"" + sDirName + "/\" /D\"_DEBUG\" /D\"WIN32\" /D\"_WINDOWS\" /D\"HAVE_AV_CONFIG_H\" /D\"inline=__inline\" /D\"strtod=avpriv_strtod\" /FI\"compat\\msvcrt\\snprintf.h\" /FR\"" + sDirName + "/\" /c /MP /w /nologo";
            uint uiStartPos = uiTotalPos;
            for( uiTotalPos; uiTotalPos < min( uiStartPos + uiRowSize, itI->second.size( ) ); uiTotalPos++ )
            {
                sCLLaunchBat += " \"../../" + itI->second[uiTotalPos] + "\"";
            }
            sCLLaunchBat += " > log.txt\nif %errorlevel% neq 0 goto exitFail\n";
        }
    }
    sCLLaunchBat += "del /F /S /Q *.obj > nul 2>&1\ndel log.txt > nul 2>&1\n";
    sCLLaunchBat += "exit /b 0\n:exitFail\nrmdir /S /Q " + sProjectNameShort + "\nexit /b 1";
    ofstream ofBatFile( "test.bat" );
    if( !ofBatFile.is_open( ) )
    {
        cout << "  Error: Failed opening temporary spawn batch file" << endl;
        return false;
    }
    ofBatFile << sCLLaunchBat;
    ofBatFile.close( );

    if( 0 != system( "test.bat" ) )
    {
        cout << "  Error: Failed calling temp.bat. Ensure you have Visual Studio or the Microsoft compiler installed and that any required dependencies are available.\nSee log.txt for further details." << endl;
        //Remove the test header files
        deleteFile( "test.bat" );
        deleteFolder( sProjectNameShort );
        return false;
    }

    //Remove the compilation objects
    deleteFile( "test.bat" );

    //Loaded in the compiler passed files
    StaticList vSBRFiles;
    StaticList vModuleExports;
    StaticList vModuleDataExports;
    findFiles( sProjectNameShort + "\\*.sbr", vSBRFiles );
    for( StaticList::iterator itSBR = vSBRFiles.begin( ); itSBR < vSBRFiles.end( ); itSBR++ )
    {
        m_ifInputFile.open( *itSBR, ios_base::in | ios_base::binary );
        if( !m_ifInputFile.is_open( ) )
        {
            cout << "  Error: Failed opening compiler output (" + *itSBR + ")" << endl;
            deleteFolder( sProjectNameShort );
            return false;
        }
        string sSBRFile;
        m_ifInputFile.seekg( 0, m_ifInputFile.end );
        uiBufferSize = (uint)m_ifInputFile.tellg( );
        m_ifInputFile.seekg( 0, m_ifInputFile.beg );
        sSBRFile.resize( uiBufferSize );
        m_ifInputFile.read( &sSBRFile[0], uiBufferSize );
        if( uiBufferSize != m_ifInputFile.gcount( ) )
        {
            sSBRFile.resize( (uint)m_ifInputFile.gcount( ) );
        }
        m_ifInputFile.close( );

        //Search through file for module exports
        for( StaticList::iterator itI = vExportStrings.begin( ); itI < vExportStrings.end( ); itI++ )
        {
            //SBR files contain data in specif formats
            // NULL SizeOfID Type Imp NULL ID Name NULL
            // where:
            // SizeOfID specifies how many characters are in the ID
            //  ETX=2
            //  C=3
            // Type specifies the type of the entry (function, data, define etc.)
            //  BEL=typedef
            //  EOT=data
            //  SOH=function
            //  ENQ=pre-processor define
            // Imp specifies if this is an declaration or a definition
            //  `=declaration
            //  @=definition
            //  STX=static or inline
            //  NULL=pre-processor
            // ID is a 2 or 3 character sequence used to uniquely identify the object

            //Check if it is a wild card search
            uiFindPos = itI->find( '*' );
            if( uiFindPos != string::npos )
            {
                //Strip the wild card (Note: assumes wild card is at the end!)
                string sSearch = itI->substr( 0, uiFindPos );

                //Search for all occurrences
                uiFindPos = sSBRFile.find( sSearch );
                while( uiFindPos != string::npos )
                {
                    //Find end of name signaled by NULL character
                    uint uiFindPos2 = sSBRFile.find( (char)0x00, uiFindPos + 1 );

                    //Check if this is a define
                    uint uiFindPos3 = sSBRFile.rfind( (char)0x00, uiFindPos-3 );
                    while( sSBRFile.at( uiFindPos3 - 1 ) == (char)0x00 )
                    {
                        //Skip if there was a NULL in ID
                        --uiFindPos3;
                    }
                    uint uiFindPosDiff = uiFindPos - uiFindPos3;
                    if( ( sSBRFile.at( uiFindPos3 - 1 ) == '@' ) &&
                        ( ( ( uiFindPosDiff == 3 ) && ( sSBRFile.at( uiFindPos3 - 3 ) == (char)0x03 ) ) ||
                        ( ( uiFindPosDiff == 4 ) && ( sSBRFile.at( uiFindPos3 - 3 ) == 'C' ) ) ) )
                    {
                        //Check if this is a data or function name
                        string sFoundName = sSBRFile.substr( uiFindPos, uiFindPos2 - uiFindPos );
                        if( ( sSBRFile.at( uiFindPos3 - 2 ) == (char)0x01 ) )
                        {
                            //This is a function
                            if( find( vModuleExports.begin( ), vModuleExports.end( ), sFoundName ) == vModuleExports.end( ) )
                            {
                                vModuleExports.push_back( sFoundName );
                            }
                        }
                        else if( ( sSBRFile.at( uiFindPos3 - 2 ) == (char)0x04 ) )
                        {
                            //This is data
                            if( find( vModuleDataExports.begin( ), vModuleDataExports.end( ), sFoundName ) == vModuleDataExports.end( ) )
                            {
                                vModuleDataExports.push_back( sFoundName );
                            }
                        }
                    }

                    //Get next
                    uiFindPos = sSBRFile.find( sSearch, uiFindPos2 + 1 );
                }
            }
            else
            {
                uiFindPos = sSBRFile.find( *itI );
                //Make sure the match is an exact one
                uint uiFindPos3;
                while( ( uiFindPos != string::npos ) )
                {
                    if( sSBRFile.at( uiFindPos + itI->length( ) ) == (char)0x00 )
                    {
                        uiFindPos3 = sSBRFile.rfind( (char)0x00, uiFindPos - 3 );
                        while( sSBRFile.at( uiFindPos3 - 1 ) == (char)0x00 )
                        {
                            //Skip if there was a NULL in ID
                            --uiFindPos3;
                        }
                        uint uiFindPosDiff = uiFindPos - uiFindPos3;
                        if( ( sSBRFile.at( uiFindPos3 - 1 ) == '@' ) &&
                            ( ( ( uiFindPosDiff == 3 ) && ( sSBRFile.at( uiFindPos3 - 3 ) == (char)0x03 ) ) ||
                            ( ( uiFindPosDiff == 4 ) && ( sSBRFile.at( uiFindPos3 - 3 ) == 'C' ) ) ) )
                        {
                            break;
                        }
                    }
                    uiFindPos = sSBRFile.find( *itI, uiFindPos + 1 );
                }
                if( uiFindPos == string::npos )
                {
                    continue;
                }
                //Check if this is a data or function name
                if( ( sSBRFile.at( uiFindPos3 - 2 ) == (char)0x01 ) )
                {
                    //This is a function
                    if( find( vModuleExports.begin( ), vModuleExports.end( ), *itI ) == vModuleExports.end( ) )
                    {
                        vModuleExports.push_back( *itI );
                    }
                }
                else if( ( sSBRFile.at( uiFindPos3 - 2 ) == (char)0x04 ) )
                {
                    //This is data
                    if( find( vModuleDataExports.begin( ), vModuleDataExports.end( ), *itI ) == vModuleDataExports.end( ) )
                    {
                        vModuleDataExports.push_back( *itI );
                    }
                }
            }
        }
    }
    //Remove the test sbr files
    deleteFolder( sProjectNameShort );

    //Check for any exported functions in asm files
    for( StaticList::iterator itASM = m_vYASMIncludes.begin(); itASM < m_vYASMIncludes.end( ); itASM++ )
    {
        m_ifInputFile.open( "../../" + *itASM, ios_base::in | ios_base::binary );
        if( !m_ifInputFile.is_open( ) )
        {
            cout << "  Error: Failed opening asm input file (" + *itASM + ")" << endl;
            return false;
        }
        string sASMFile;
        m_ifInputFile.seekg( 0, m_ifInputFile.end );
        uiBufferSize = (uint)m_ifInputFile.tellg( );
        m_ifInputFile.seekg( 0, m_ifInputFile.beg );
        sASMFile.resize( uiBufferSize );
        m_ifInputFile.read( &sASMFile[0], uiBufferSize );
        if( uiBufferSize != m_ifInputFile.gcount( ) )
        {
            sASMFile.resize( (uint)m_ifInputFile.gcount( ) );
        }
        m_ifInputFile.close( );

        //Search through file for module exports
        for( StaticList::iterator itI = vExportStrings.begin( ); itI < vExportStrings.end( ); itI++ )
        {
            //Check if it is a wild card search
            uiFindPos = itI->find( '*' );
            string sInvalidChars = ",.(){}[]`'\"+-*/!@#$%^&*<>|;\\= \n\t\0";
            if( uiFindPos != string::npos )
            {
                //Strip the wild card (Note: assumes wild card is at the end!)
                string sSearch = ' ' + itI->substr( 0, uiFindPos );
                //Search for all occurrences
                uiFindPos = sASMFile.find( sSearch );
                while( ( uiFindPos != string::npos ) && ( uiFindPos > 0 ) )
                {
                    //Find end of name signaled by first non valid character
                    uint uiFindPos2 = sASMFile.find_first_of( sInvalidChars, uiFindPos + 1 );
                    //Check this is valid function definition
                    if( ( sASMFile.at( uiFindPos2 ) == '(' ) && ( sInvalidChars.find( sASMFile.at( uiFindPos-1 ) ) == string::npos ) )
                    {
                        string sFoundName = sASMFile.substr( uiFindPos, uiFindPos2 - uiFindPos );
                        if( find( vModuleExports.begin( ), vModuleExports.end( ), sFoundName ) == vModuleExports.end( ) )
                        {
                            vModuleExports.push_back( sFoundName.substr( 1 ) );
                        }
                    }

                    //Get next
                    uiFindPos = sASMFile.find( sSearch, uiFindPos2 + 1 );
                }
            }
            else
            {
                string sSearch = ' ' + *itI + '(';
                uiFindPos = sASMFile.find( *itI );
                //Make sure the match is an exact one
                if( ( uiFindPos != string::npos ) && ( uiFindPos > 0 ) && ( sInvalidChars.find( sASMFile.at( uiFindPos - 1 ) ) == string::npos ) )
                {
                    //Check this is valid function definition
                    if( find( vModuleExports.begin( ), vModuleExports.end( ), *itI ) == vModuleExports.end( ) )
                    {
                        vModuleExports.push_back( *itI );
                    }
                }
            }
        }
    }


    //Sort the exports
    sort( vModuleExports.begin( ), vModuleExports.end( ) );
    sort( vModuleDataExports.begin( ), vModuleDataExports.end( ) );

    //Create the export module string
    string sModuleFile = "EXPORTS\n";
    for( StaticList::iterator itI = vModuleExports.begin( ); itI < vModuleExports.end( ); itI++ )
    {
        sModuleFile += "    " + *itI + "\n";
    }
    for( StaticList::iterator itI = vModuleDataExports.begin( ); itI < vModuleDataExports.end( ); itI++ )
    {
        sModuleFile += "    " + *itI + " DATA\n";
    }

    string sDestinationFile = "../../" + sProjectName + ".def";
    ofstream ofModuleFile( sDestinationFile );
    if( !ofModuleFile.is_open( ) )
    {
        cout << "  Error: Failed opening output module definition file (" << sDestinationFile << ")" << endl;
        return false;
    }

    //Output module file and close
    ofModuleFile << sModuleFile;
    ofModuleFile.close( );

    return true;
}

bool projectGenerator::outputSolution()
{
    cout << "  Generating solution file..." << endl;
    //Open the input temp project file
    m_ifInputFile.open( "../templates/template_in.sln" );
    if( !m_ifInputFile.is_open( ) )
    {
        cout << "  Error: Failed opening template solution (../templates/template_in.sln)" << endl;
        return false;
    }
    //Load whole file into internal string
    string sSolutionFile;
    m_ifInputFile.seekg( 0, m_ifInputFile.end );
    uint uiBufferSize = (uint)m_ifInputFile.tellg();
    m_ifInputFile.seekg( 0, m_ifInputFile.beg );
    sSolutionFile.resize( uiBufferSize );
    m_ifInputFile.read( &sSolutionFile[0], uiBufferSize );
    if( uiBufferSize != m_ifInputFile.gcount() )
    {
        sSolutionFile.resize( (uint)m_ifInputFile.gcount() );
    }
    m_ifInputFile.close( );

    map<string, string> mLibKeys;
    map<string, string> mProgramKeys;
    buildProjectGUIDs( mLibKeys, mProgramKeys );
    string sSolutionKey = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942";

    vector<string> vAddedKeys;

    string sProject = "\nProject(\"{";
    string sProject2 = "}\") = \"";
    string sProject3 = "\", \"";
    string sProject4 = ".vcxproj\", \"{";
    string sProjectEnd = "}\"";
    string sProjectClose = "\nEndProject";

    string sDepend = "\n	ProjectSection(ProjectDependencies) = postProject";
    string sDependClose = "\n	EndProjectSection";
    string sSubDepend =	"\n		{";
    string sSubDepend2 = "} = {";
    string sSubDependEnd = "}";

    //Find the start of the file
    string sFileStart = "Project";
    uint uiPos = sSolutionFile.find( sFileStart ) - 1;

    map<string,StaticList>::iterator mitLibraries = m_mProjectLibs.begin();
    while( mitLibraries != m_mProjectLibs.end( ) )
    {
        //Check if this library has a known key (to lazy to auto generate at this time)
        if( mLibKeys.find( mitLibraries->first ) == mLibKeys.end( ) )
        {
            cout << "  Error: Unknown library. Could not determine solution key (" << mitLibraries->first << ")" << endl;
            return false;
        }
        //Add the library to the solution
        string sProjectAdd = sProject;
        sProjectAdd += sSolutionKey;
        sProjectAdd += sProject2;
        sProjectAdd += mitLibraries->first;
        sProjectAdd += sProject3;
        sProjectAdd += mitLibraries->first;
        sProjectAdd += sProject4;
        sProjectAdd += mLibKeys[mitLibraries->first];
        sProjectAdd += sProjectEnd;

        //Add the key to the used key list
        vAddedKeys.push_back( mLibKeys[mitLibraries->first] );

        //Add the dependencies
        if( mitLibraries->second.size() > 0 )
        {
            sProjectAdd += sDepend;
            for( StaticList::iterator vitIt=mitLibraries->second.begin(); vitIt<mitLibraries->second.end(); vitIt++ )
            {
                //Check if this library has a known key
                if( mLibKeys.find( *vitIt ) == mLibKeys.end( ) )
                {
                    cout << "  Error: Unknown library dependency. Could not determine solution key (" << *vitIt << ")" << endl;
                    return false;
                }
                sProjectAdd += sSubDepend;
                sProjectAdd += mLibKeys[*vitIt];
                sProjectAdd += sSubDepend2;
                sProjectAdd += mLibKeys[*vitIt];
                sProjectAdd += sSubDependEnd;
            }
            sProjectAdd += sDependClose;
        }
        sProjectAdd += sProjectClose;

        //Insert into solution string
        sSolutionFile.insert( uiPos, sProjectAdd );
        uiPos += sProjectAdd.length( );

        //next
        ++mitLibraries;
    }

    //Create program list
    map<string, string> mProgramList;
    if( !m_ConfigHelper.m_bLibav )
    {
        mProgramList["ffmpeg"] = "CONFIG_FFMPEG";
        mProgramList["ffplay"] = "CONFIG_FFPLAY";
        mProgramList["ffprobe"] = "CONFIG_FFPROBE";
    }
    else
    {
        mProgramList["avconv"] = "CONFIG_AVCONV";
        mProgramList["avplay"] = "CONFIG_AVPLAY";
        mProgramList["avprobe"] = "CONFIG_AVPROBE";
    }

    //Next add the projects
    string sToolchain;
    if( !passToolchain( sToolchain ) )
    {
        return false;
    }
    //If toolchain is newer than 2013 then add additional toolsets
    if( sToolchain.compare( "v150" ) == 0 )
    {
        sToolchain = "v120</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='14.0'\">v140</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='15.0'\">v150";
    }
    else if( sToolchain.compare( "v140" ) == 0 )
    {
        sToolchain = "v120</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='14.0'\">v140";
    }
    string sProjectAdd;
    vector<string> vAddedPrograms;
    map<string,string>::iterator mitPrograms = mProgramList.begin();
    while( mitPrograms != mProgramList.end( ) )
    {
        //Check if program is enabled
        const string sDestinationFile = "../../" + mitPrograms->first + ".vcxproj";
        const string sDestinationFilterFile = "../../" + mitPrograms->first + ".vcxproj.filters";
        if( m_ConfigHelper.getConfigOptionPrefixed( mitPrograms->second )->m_sValue.compare("1") == 0 )
        {
            //Open the template program
            const string sSourceFile = "../templates/templateprogram_in.vcxproj";
            //Open the input program file
            m_ifInputFile.open( sSourceFile );
            if( !m_ifInputFile.is_open( ) )
            {
                cout << "  Error: Failed opening program template (" << sSourceFile << ")" << endl;
                return false;
            }
            //Load whole file into internal string
            string sProgramFile;
            m_ifInputFile.seekg( 0, m_ifInputFile.end );
            uint uiBufferSize = (uint)m_ifInputFile.tellg();
            m_ifInputFile.seekg( 0, m_ifInputFile.beg );
            sProgramFile.resize( uiBufferSize );
            m_ifInputFile.read( &sProgramFile[0], uiBufferSize );
            if( uiBufferSize != m_ifInputFile.gcount() )
            {
                sProgramFile.resize( (uint)m_ifInputFile.gcount() );
            }
            m_ifInputFile.close( );
            //Open the template program filters
            const string sFiltersFile = "../templates/templateprogram_in.vcxproj.filters";
            //Open the input program file
            m_ifInputFile.open( sFiltersFile );
            if( !m_ifInputFile.is_open( ) )
            {
                cout << "  Error: Failed opening program filters template (" << sFiltersFile << ")" << endl;
                return false;
            }
            //Load whole file into internal string
            string sProgramFiltersFile;
            m_ifInputFile.seekg( 0, m_ifInputFile.end );
            uiBufferSize = (uint)m_ifInputFile.tellg( );
            m_ifInputFile.seekg( 0, m_ifInputFile.beg );
            sProgramFiltersFile.resize( uiBufferSize );
            m_ifInputFile.read( &sProgramFiltersFile[0], uiBufferSize );
            if( uiBufferSize != m_ifInputFile.gcount( ) )
            {
                sProgramFiltersFile.resize( (uint)m_ifInputFile.gcount( ) );
            }
            m_ifInputFile.close( );
            //Change all occurance of template_in with project name
            const string sFFSearchTag = "template_in";
            uint uiFindPos = sProgramFile.find( sFFSearchTag );
            while( uiFindPos != string::npos )
            {
                //Replace
                sProgramFile.replace( uiFindPos, sFFSearchTag.length( ), mitPrograms->first );
                //Get next
                uiFindPos = sProgramFile.find( sFFSearchTag, uiFindPos + 1 );
            }
            uint uiFindPosFilt = sProgramFiltersFile.find( sFFSearchTag );
            while( uiFindPosFilt != string::npos )
            {
                //Replace
                sProgramFiltersFile.replace( uiFindPosFilt, sFFSearchTag.length( ), mitPrograms->first );
                //Get next
                uiFindPosFilt = sProgramFiltersFile.find( sFFSearchTag, uiFindPosFilt + 1 );
            }
            //Change all occurance of template_platform with specified project toolchain
            const string sPlatformSearch = "template_platform";
            uiFindPos = sProgramFile.find( sPlatformSearch );
            while( uiFindPos != string::npos )
            {
                //Replace
                sProgramFile.replace( uiFindPos, sPlatformSearch.length( ), sToolchain );
                //Get next
                uiFindPos = sProgramFile.find( sPlatformSearch, uiFindPos + 1 );
            }
            //Set the project key
            string sGUID = "<ProjectGuid>{";
            uiFindPos = sProgramFile.find( sGUID );
            if( uiFindPos != string::npos )
            {
                uiFindPos += sGUID.length( );
                sProgramFile.replace( uiFindPos, mProgramKeys[mitPrograms->first].length( ), mProgramKeys[mitPrograms->first] );
            }
            //Add the required source files
            string sItemGroup = "\n  <ItemGroup>";
            string sItemGroupEnd = "\n  </ItemGroup>";
            uiFindPos = sProgramFile.find( sItemGroupEnd );
            uiFindPos += sItemGroupEnd.length( );
            uiFindPosFilt = sProgramFiltersFile.find( sItemGroupEnd );
            uiFindPosFilt += sItemGroupEnd.length( );
            string sCLCompile = "\n    <ClCompile Include=\"";
            string sCLCompileClose = "\">";
            string sCLCompileObject = "\n      <ObjectFileName>$(IntDir)\\";
            string sCLCompileObjectClose = ".obj</ObjectFileName>";
            string sCLCompileEnd = "\n    </ClCompile>";
            string sFilterSource = "\n      <Filter>Source Files";
            string sSource = "Source Files";
            string sFilterEnd = "</Filter>";
            vector<string> vCIncludes;
            vector<string> vHIncludes;
            vector<string> vLibs;
            vector<string> vIncDirs;
            vector<string> vLib32Dirs;
            vector<string> vLib64Dirs;
            buildProgramIncludes( mitPrograms->first, vCIncludes, vHIncludes, vLibs, vIncDirs, vLib32Dirs, vLib64Dirs );
            string sCFiles = sItemGroup;
            string sCFilesFilt = sItemGroup;
            for( StaticList::iterator vitInclude = vCIncludes.begin( ); vitInclude < vCIncludes.end( ); vitInclude++ )
            {
                //Output CLCompile objects
                sCFiles += sCLCompile;
                sCFilesFilt += sCLCompile;
                //Add the fileName
                string sFileName = *vitInclude;
                replace( sFileName.begin( ), sFileName.end( ), '/', '\\' );
                sCFiles += sFileName;
                sCFilesFilt += sFileName;
                sCFiles += sCLCompileClose;
                sCFilesFilt += sCLCompileClose;
                //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
                uint uiPos = sFileName.rfind( "..\\" );
                uiPos = ( uiPos == string::npos ) ? 0 : uiPos + 3;
                string sObjectName = sFileName.substr( uiPos );
                replace( sObjectName.begin( ), sObjectName.end( ), '\\', '_' );
                //Replace the extension with obj
                uint uiPos2 = sObjectName.rfind( '.' );
                sObjectName.resize( uiPos2 );
                sCFiles += sCLCompileObject;
                sCFiles += sObjectName;
                sCFiles += sCLCompileObjectClose;
                //Add the filters Filter
                sCFilesFilt += sFilterSource;
                sCFilesFilt += sFilterEnd;
                //Close the current item
                sCFiles += sCLCompileEnd;
                sCFilesFilt += sCLCompileEnd;
            }
            sCFiles += sItemGroupEnd;
            sCFilesFilt += sItemGroupEnd;
            //Insert into output file
            sProgramFile.insert( uiFindPos, sCFiles );
            uiFindPos += sCFiles.length( );
            sProgramFiltersFile.insert( uiFindPosFilt, sCFilesFilt );
            uiFindPosFilt += sCFilesFilt.length( );
            //Add the required header files
            string sCLInclude = "\n    <ClInclude Include=\"";
            string sCLIncludeEnd = "\" />";
            string sCLIncludeEndFilt = "\">";
            string sCLIncludeClose = "\n    </ClInclude>";
            string sFilterHeader = "\n      <Filter>Header Files";
            string sHeaders = "Header Files";
            string sHFiles = sItemGroup;
            string sHFilesFilt = sItemGroup;
            for( StaticList::iterator vitInclude = vHIncludes.begin( ); vitInclude < vHIncludes.end( ); vitInclude++ )
            {
                //Output CLInclude objects
                sHFiles += sCLInclude;
                sHFilesFilt += sCLInclude;
                //Add the fileName
                string sFileName = *vitInclude;
                replace( sFileName.begin( ), sFileName.end( ), '/', '\\' );
                sHFiles += sFileName;
                sHFilesFilt += sFileName;
                //Close the current item
                sHFiles += sCLIncludeEnd;
                sHFilesFilt += sCLIncludeEndFilt;
                //Add the filters Filter
                sHFilesFilt += sFilterHeader;
                sHFilesFilt += sFilterEnd;
                sHFilesFilt += sCLIncludeClose;
            }
            sHFiles += sItemGroupEnd;
            sHFilesFilt += sItemGroupEnd;
            //Insert into output file
            sProgramFile.insert( uiFindPos, sHFiles );
            uiFindPos += sHFiles.length( );
            sProgramFiltersFile.insert( uiFindPosFilt, sHFilesFilt );
            uiFindPosFilt += sHFilesFilt.length( );
            //Add the required lib dependencies
            string sDeps[2]; //debug, release
            map<string, StaticList>::iterator mitLibraries = m_mProjectLibs.begin( );
            while( mitLibraries != m_mProjectLibs.end( ) )
            {
                sDeps[0] += mitLibraries->first;
                sDeps[0] += "d.lib;";
                sDeps[1] += mitLibraries->first;
                sDeps[1] += ".lib;";
                mitLibraries++;
            }
            for( vector<string>::iterator vitDeps = vLibs.begin( ); vitDeps < vLibs.end( ); vitDeps++ )
            {
                sDeps[0] += *vitDeps + ";";
                sDeps[1] += *vitDeps + ";";
            }
            const string sAddDeps = "%(AdditionalDependencies)";
            uiFindPos = sProgramFile.find( sAddDeps );
            //loop over debug, release
            for( uint uDepPos = 0; uDepPos < 2; uDepPos++ )
            {
                //loop over static, shared in win32/x64
                for( uint uI = 0; uI < 4; uI++ )
                {
                    if( uiFindPos == string::npos )
                    {
                        cout << "  Error: Failed finding dependencies in program template." << endl;
                        return false;
                    }
                    //Add to output
                    sProgramFile.insert( uiFindPos, sDeps[uDepPos] );
                    uiFindPos += sDeps[uDepPos].length( );
                    //Get next
                    uiFindPos = sProgramFile.find( sAddDeps, uiFindPos + 1 );
                }
            }
            //Add required include directories
            string sAddIncDirs = "";
            for( vector<string>::iterator vitDirs = vIncDirs.begin( ); vitDirs < vIncDirs.end( ); vitDirs++ )
            {
                sAddIncDirs += *vitDirs + ";";
            }
            if( sAddIncDirs.length( ) > 0 )
            {
                const string sAddIncs = "%(AdditionalIncludeDirectories)";
                uiFindPos = sProgramFile.find( sAddIncs );
                while( uiFindPos != string::npos )
                {
                    //Add to output
                    sProgramFile.insert( uiFindPos, sAddIncDirs );
                    uiFindPos += sAddIncDirs.length( );
                    //Get next
                    uiFindPos = sProgramFile.find( sAddIncs, uiFindPos + 1 );
                }
            }
            //Add lib includes
            if( ( vLib32Dirs.size( ) > 0 ) || ( vLib64Dirs.size( ) > 0 ) )
            {
                //Add additional lib includes to include list based on current config
                string sAddLibs[2];
                for( StaticList::iterator vitIt = vLib32Dirs.begin( ); vitIt < vLib32Dirs.end( ); vitIt++ )
                {
                    sAddLibs[0] += *vitIt + ";";
                }
                for( StaticList::iterator vitIt = vLib64Dirs.begin( ); vitIt < vLib64Dirs.end( ); vitIt++ )
                {
                    sAddLibs[1] += *vitIt + ";";
                }
                string sAddLibDir = "<AdditionalLibraryDirectories>";
                uint ui32Or64 = 0; //start with 32 (assumes projects are ordered 32 then 64 recursive)
                uiFindPos = sProgramFile.find( sAddLibDir );
                while( uiFindPos != string::npos )
                {
                    //Add to output
                    uiFindPos += sAddLibDir.length( );
                    sProgramFile.insert( uiFindPos, sAddLibs[ui32Or64] );
                    uiFindPos += sAddLibs[ui32Or64].length( );
                    //Get next
                    uiFindPos = sProgramFile.find( sAddLibDir, uiFindPos + 1 );
                    ui32Or64 = !ui32Or64;
                }
            }

            //Output program file and close
            ofstream ofProjectFile( sDestinationFile );
            if( !ofProjectFile.is_open( ) )
            {
                cout << "  Error: failed opening output project file (" << sDestinationFile << ")" << endl;
                return false;
            }

            //Output project file and close
            ofProjectFile << sProgramFile;
            ofProjectFile.close( );

            //Open output filters
            ofstream ofFiltersFile( sDestinationFilterFile );
            if( !ofFiltersFile.is_open( ) )
            {
                cout << "  Error: failed opening output project filters file (" << sDestinationFilterFile << ")" << endl;
                return false;
            }

            //Output project filter file and close
            ofFiltersFile << sProgramFiltersFile;
            ofFiltersFile.close( );

            //Add the program to the solution
            sProjectAdd += sProject;
            sProjectAdd += sSolutionKey;
            sProjectAdd += sProject2;
            sProjectAdd += mitPrograms->first;
            sProjectAdd += sProject3;
            sProjectAdd += mitPrograms->first;
            sProjectAdd += sProject4;
            sProjectAdd += mProgramKeys[mitPrograms->first];
            sProjectAdd += sProjectEnd;

            //Add the key to the used key list
            vAddedPrograms.push_back( mProgramKeys[mitPrograms->first] );

            //Add the dependencies
            sProjectAdd += sDepend;
            map<string,StaticList>::iterator mitLibs = m_mProjectLibs.begin();
            while( mitLibs != m_mProjectLibs.end( ) )
            {
                //Add all project libraries as dependencies (except avresample with ffmpeg)
                if( !m_ConfigHelper.m_bLibav && mitLibs->first.compare( "avresample" ) != 0 )
                {
                    sProjectAdd += sSubDepend;
                    sProjectAdd += mLibKeys[mitLibs->first];
                    sProjectAdd += sSubDepend2;
                    sProjectAdd += mLibKeys[mitLibs->first];
                    sProjectAdd += sSubDependEnd;
                }
                //next
                ++mitLibs;
            }
            sProjectAdd += sDependClose;
            sProjectAdd += sProjectClose;
        }
        else
        {
            //Delete any existing to avoid pollution
            deleteFile( sDestinationFile );
            deleteFile( sDestinationFilterFile );
        }
        //next
        ++mitPrograms;
    }

    //Check if there were actually any programs added
    string sProgramKey = "8A736DDA-6840-4E65-9DA4-BF65A2A70428";
    if( sProjectAdd.length( ) > 0 )
    {
        //Add program key
        sProjectAdd += "\nProject(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Programs\", \"Programs\", \"{";
        sProjectAdd += sProgramKey;
        sProjectAdd += "}\"";
        sProjectAdd += "\nEndProject";

        //Insert into solution string
        sSolutionFile.insert( uiPos, sProjectAdd );
        uiPos += sProjectAdd.length( );
    }

    //Next Add the solution configurations
    string sConfigStart = "GlobalSection(ProjectConfigurationPlatforms) = postSolution";
    uiPos = sSolutionFile.find( sConfigStart ) + sConfigStart.length( );
    string sConfigPlatform = "\n		{";
    string sConfigPlatform2 = "}.";
    string sConfigPlatform3 = "|";
    string aBuildConfigs[7] = { "Debug", "DebugDLL", "DebugDLLStaticDeps", "Release", "ReleaseDLL", "ReleaseDLLStaticDeps", "ReleaseLTO" };
    string aBuildArchs[2] = { "Win32", "x64" };
    string aBuildTypes[2] = { ".ActiveCfg = ", ".Build.0 = " };
    string sAddPlatform;
    //Add the lib keys
    for( vector<string>::iterator vitIt=vAddedKeys.begin(); vitIt<vAddedKeys.end(); vitIt++ )
    {
        //loop over build configs
        for( uint uiI=0; uiI<7; uiI++ )
        {
            //loop over build archs
            for( uint uiJ=0; uiJ<2; uiJ++ )
            {
                //loop over build types
                for( uint uiK=0; uiK<2; uiK++ )
                {
                    sAddPlatform += sConfigPlatform;
                    sAddPlatform += *vitIt;
                    sAddPlatform += sConfigPlatform2;
                    sAddPlatform += aBuildConfigs[uiI];
                    sAddPlatform += sConfigPlatform3;
                    sAddPlatform += aBuildArchs[uiJ];
                    sAddPlatform += aBuildTypes[uiK];
                    sAddPlatform += aBuildConfigs[uiI];
                    sAddPlatform += sConfigPlatform3;
                    sAddPlatform += aBuildArchs[uiJ];
                }
            }
        }
    }
    //Add the program keys
    for( vector<string>::iterator vitIt = vAddedPrograms.begin( ); vitIt < vAddedPrograms.end( ); vitIt++ )
    {
        //loop over build configs
        for( uint uiI=0; uiI<7; uiI++ )
        {
            //loop over build archs
            for( uint uiJ=0; uiJ<2; uiJ++ )
            {
                //loop over build types
                for( uint uiK=0; uiK<2; uiK++ )
                {
                    sAddPlatform += sConfigPlatform;
                    sAddPlatform += *vitIt;
                    sAddPlatform += sConfigPlatform2;
                    sAddPlatform += aBuildConfigs[uiI];
                    sAddPlatform += sConfigPlatform3;
                    sAddPlatform += aBuildArchs[uiJ];
                    sAddPlatform += aBuildTypes[uiK];
                    if( uiI == 2 )
                    {
                        sAddPlatform += aBuildConfigs[1];
                    }
                    else if( uiI == 5 )
                    {
                        sAddPlatform += aBuildConfigs[4];
                    }
                    else if( uiI == 6 )
                    {
                        //there is no program lto build so use release instead
                        sAddPlatform += aBuildConfigs[3];
                    }
                    else
                    {
                        sAddPlatform += aBuildConfigs[uiI];
                    }
                    sAddPlatform += sConfigPlatform3;
                    sAddPlatform += aBuildArchs[uiJ];
                }
            }
        }
    }
    //Insert into solution string
    sSolutionFile.insert( uiPos, sAddPlatform );
    uiPos += sAddPlatform.length( );

    //Add any programs to the nested projects
    if( vAddedPrograms.size() > 0 )
    {
        string sNestedStart = "GlobalSection(NestedProjects) = preSolution";
        uint uiPos = sSolutionFile.find( sNestedStart ) + sNestedStart.length( );
        string sNest = "\n		{";
        string sNest2 = "} = {";
        string sNestEnd = "}";
        string sNestProg;
        for( vector<string>::iterator vitIt=vAddedPrograms.begin(); vitIt<vAddedPrograms.end(); vitIt++ )
        {
            sNestProg += sNest;
            sNestProg += *vitIt;
            sNestProg += sNest2;
            sNestProg += sProgramKey;
            sNestProg += sNestEnd;
        }
        //Insert into solution string
        sSolutionFile.insert( uiPos, sNestProg );
        uiPos += sNestProg.length( );
    }

    //Open output solution
    string sProjectName = m_ConfigHelper.m_sProjectName;
    transform( sProjectName.begin( ), sProjectName.end( ), sProjectName.begin( ), ::tolower );
    const string sOutSolutionFile = "../../" + sProjectName + ".sln";
    ofstream ofSolutionFile( sOutSolutionFile );
    if( !ofSolutionFile.is_open( ) )
    {
        cout << "  Error: Failed opening output solution file (" << sOutSolutionFile << ")" << endl;
        return false;
    }

    //Output solution file and close
    ofSolutionFile << sSolutionFile;
    ofSolutionFile.close( );

    return true;

}

bool projectGenerator::passStaticIncludeObject( uint & uiStartPos, uint & uiEndPos, StaticList & vStaticIncludes )
{
    //Add the found string to internal storage
    uiEndPos = m_sInLine.find_first_of( ". \t", uiStartPos );
    string sTag = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
    if( sTag.find( '$' ) != string::npos )
    {
        // Invalid include. Occurs when include is actually a variable
        uiStartPos += 2;
        sTag = m_sInLine.substr( uiStartPos, m_sInLine.find( ')', uiStartPos ) - uiStartPos );
        // Check if additional variable (This happens when a string should be prepended to existing items within tag.)
        string sTag2;
        if( sTag.find( ':' ) != string::npos )
        {
            uiStartPos = sTag.find( ":%=" );
            uint uiStartPos2 = uiStartPos + 3;
            uiEndPos = sTag.find( '%', uiStartPos2 );
            sTag2 = sTag.substr( uiStartPos2, uiEndPos - uiStartPos2 );
            sTag = sTag.substr( 0, uiStartPos );
        }
        // Get variable contents
        vector<string> vFiles;
        m_ConfigHelper.buildObjects( sTag, vFiles );
        if( sTag2.length( ) > 0 )
        {
            //Prepend the full library path
            for( vector<string>::iterator vitFile = vFiles.begin( ); vitFile<vFiles.end( ); vitFile++ )
            {
                *vitFile = sTag2 + *vitFile;
            }
        }
        //Loop through each item and add to list
        for( vector<string>::iterator vitFile=vFiles.begin(); vitFile<vFiles.end(); vitFile++ )
        {
            //Check if object already included in internal list
            if( find( m_vCIncludes.begin(), m_vCIncludes.end(), *vitFile ) == m_vCIncludes.end() )
            {
                vStaticIncludes.push_back( *vitFile );
                //cout << "  Found C Static: '" << *vitFile << "'" << endl;
            }
        }
        return true;
    }

    //Check if object already included in internal list
    if( find( vStaticIncludes.begin(), vStaticIncludes.end(), sTag ) == vStaticIncludes.end() )
    {
        vStaticIncludes.push_back( sTag );
        //cout << "  Found Static: '" << sTag << "'" << endl;
    }
    return true;
}

bool projectGenerator::passStaticIncludeLine( uint uiStartPos, StaticList & vStaticIncludes )
{
    uint uiEndPos;
    if( !passStaticIncludeObject( uiStartPos, uiEndPos, vStaticIncludes ) )
    {
        return false;
    }
    //Check if there are multiple files declared on the same line
    while( uiEndPos != string::npos )
    {
        uiStartPos = m_sInLine.find_first_of( " \t\\\n\0", uiEndPos );
        uiStartPos = m_sInLine.find_first_not_of( " \t\\\n\0", uiStartPos );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passStaticIncludeObject( uiStartPos, uiEndPos, vStaticIncludes ) )
        {
            return false;
        }
    }
    return true;
}

bool projectGenerator::passStaticInclude( uint uiILength, StaticList & vStaticIncludes )
{
    //Remove the identifier and '='
    uint uiStartPos = m_sInLine.find_first_not_of( " +=", uiILength );
    if( !passStaticIncludeLine( uiStartPos, vStaticIncludes ) )
    {
        return true;
    }
    //Check if this is a multi line declaration
    while( m_sInLine.back() == '\\' )
    {
        //Get the next line
        getline( m_ifInputFile, m_sInLine );
        //Remove the whitespace
        uiStartPos = m_sInLine.find_first_not_of( " \t" );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passStaticIncludeLine( uiStartPos, vStaticIncludes ) )
        {
            return true;
        }
    }
    return true;
}

bool projectGenerator::passDynamicIncludeObject( uint & uiStartPos, uint & uiEndPos, string & sIdent, StaticList & vIncludes )
{
    //Check if this is A valid File or a past compile option
    if( m_sInLine.at( uiStartPos ) == '$' )
    {
        uiEndPos = m_sInLine.find( ')', uiStartPos );
        string sDynInc = m_sInLine.substr( uiStartPos+2, uiEndPos-uiStartPos-2 );
        //Find it in the unknown list
        UnknownList::iterator mitObjectList = m_mUnknowns.find( sDynInc );
        if( mitObjectList != m_mUnknowns.end( ) )
        {
            //Loop over each internal object
            for( StaticList::iterator vitObject=mitObjectList->second.begin(); vitObject<mitObjectList->second.end(); vitObject++ )
            {
                //Check if object already included in internal list
                if( find( vIncludes.begin(), vIncludes.end(), *vitObject ) == vIncludes.end() )
                {
                    //Check if the config option is correct
                    configGenerator::ValuesList::iterator vitOption = m_ConfigHelper.getConfigOptionPrefixed( sIdent );
                    if( vitOption == m_ConfigHelper.m_vConfigValues.end( ) )
                    {
                        cout << "  Warning: Unknown dynamic configuration option (" << sIdent << ") used when passing object (" << *vitObject << ")" << endl;
                        return true;
                    }
                    if( vitOption->m_sValue.compare("1") == 0 )
                    {
                            vIncludes.push_back( *vitObject );
                            //cout << "  Found Dynamic: '" << *vitObject << "', '" << "( " + sIdent + " && " + sDynInc + " )" << "'" << endl;
                    }
                }
            }
        }
        else
        {
            cout << "  Error: Found unknown token (" << sDynInc << ")" << endl;
            return false;
        }
    }
    else if( m_sInLine.at( uiStartPos ) == '#' )
    {
        //Found a comment, just skip till end of line
        uiEndPos = m_sInLine.length( );
        return true;
    }
    else
    {
        //Check for condition
        string sCompare = "1";
        if( sIdent.at( 0 ) == '!' )
        {
            sIdent = sIdent.substr(1);
            sCompare = "0";
        }
        uiEndPos = m_sInLine.find_first_of( ". \t", uiStartPos );
        //Add the found string to internal storage
        string sTag = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
        //Check if object already included in internal list
        if( find( vIncludes.begin(), vIncludes.end(), sTag ) == vIncludes.end() )
        {
            //Check if the config option is correct
            configGenerator::ValuesList::iterator vitOption = m_ConfigHelper.getConfigOptionPrefixed( sIdent );
            if( vitOption == m_ConfigHelper.m_vConfigValues.end( ) )
            {
                cout << "  Warning: Unknown dynamic configuration option (" << sIdent << ") used when passing object (" << sTag << ")" << endl;
                return true;
            }
            if( vitOption->m_sValue.compare( sCompare ) == 0 )
            {
                vIncludes.push_back( sTag );
                //cout << "  Found Dynamic: '" << sTag << "', '" << sIdent << "'" << endl;
            }
        }
    }
    return true;
}

bool projectGenerator::passDynamicIncludeLine( uint uiStartPos, string & sIdent, StaticList & vIncludes )
{
    uint uiEndPos;
    if( !passDynamicIncludeObject( uiStartPos, uiEndPos, sIdent, vIncludes ) )
    {
        return false;
    }
    //Check if there are multiple files declared on the same line
    while( uiEndPos != string::npos )
    {
        uiStartPos = m_sInLine.find_first_of( " \t\\\n\0", uiEndPos );
        uiStartPos = m_sInLine.find_first_not_of( " \t\\\n\0", uiStartPos );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passDynamicIncludeObject( uiStartPos, uiEndPos, sIdent, vIncludes ) )
        {
            return false;
        }
    }
    return true;
}

bool projectGenerator::passDynamicInclude( uint uiILength, StaticList & vIncludes )
{
    //Find the dynamic identifier
    uint uiStartPos = m_sInLine.find_first_not_of( "$( \t", uiILength );
    uint uiEndPos = m_sInLine.find( ')' );
    string sIdent = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
    //Find the included obj
    uiStartPos = m_sInLine.find_first_not_of( "+= \t", uiEndPos+1 );
    if( !passDynamicIncludeLine( uiStartPos, sIdent, vIncludes ) )
    {
        return false;
    }
    //Check if this is a multi line declaration
    while( m_sInLine.back() == '\\' )
    {
        //Get the next line
        getline( m_ifInputFile, m_sInLine );
        //Remove the whitespace
        uiStartPos = m_sInLine.find_first_not_of( " \t" );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passDynamicIncludeLine( uiStartPos, sIdent, vIncludes ) )
        {
            return false;
        }
    }
    return true;
}

bool projectGenerator::passCInclude( )
{
    return passStaticInclude( 4, m_vIncludes );
}

bool projectGenerator::passDCInclude( )
{
    return passDynamicInclude( 5, m_vIncludes );
}

bool projectGenerator::passYASMInclude( )
{
    //Check if supported option
    if( m_ConfigHelper.getConfigOptionPrefixed( "HAVE_YASM" )->m_sValue.compare("1") == 0 )
    {
        return passStaticInclude( 9, m_vIncludes );
    }
    return true;
}

bool projectGenerator::passDYASMInclude( )
{
    //Check if supported option
    if( m_ConfigHelper.getConfigOptionPrefixed( "HAVE_YASM" )->m_sValue.compare("1") == 0 )
    {
        return passDynamicInclude( 10, m_vIncludes );
    }
    return true;
}

bool projectGenerator::passMMXInclude( )
{
    //Check if supported option
    if( m_ConfigHelper.getConfigOptionPrefixed( "HAVE_MMX" )->m_sValue.compare("1") == 0 )
    {
        return passStaticInclude( 8, m_vIncludes );
    }
    return true;
}

bool projectGenerator::passDMMXInclude( )
{
    //Check if supported option
    if( m_ConfigHelper.getConfigOptionPrefixed( "HAVE_MMX" )->m_sValue.compare("1") == 0 )
    {
        return passDynamicInclude( 9, m_vIncludes );
    }
    return true;
}

bool projectGenerator::passHInclude( )
{
    return passStaticInclude( 7, m_vHIncludes );
}

bool projectGenerator::passDHInclude( )
{
    return passDynamicInclude( 8, m_vHIncludes );
}

bool projectGenerator::passLibInclude( )
{
    return passStaticInclude( 6, m_vLibs );
}

bool projectGenerator::passDLibInclude( )
{
    return passDynamicInclude( 7, m_vLibs );
}

bool projectGenerator::passDUnknown( )
{
    //Find the dynamic identifier
    uint uiStartPos = m_sInLine.find( "$(" );
    uint uiEndPos = m_sInLine.find( ')', uiStartPos );
    string sPrefix = m_sInLine.substr( 0, uiStartPos ) + "yes";
    uiStartPos += 2; //Skip the $(
    string sIdent = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
    //Find the included obj
    uiStartPos = m_sInLine.find_first_not_of( "+= \t", uiEndPos+1 );
    if( !passDynamicIncludeLine( uiStartPos, sIdent, m_mUnknowns[sPrefix] ) )
    {
        return false;
    }
    //Check if this is a multi line declaration
    while( m_sInLine.back() == '\\' )
    {
        //Get the next line
        getline( m_ifInputFile, m_sInLine );
        //Remove the whitespace
        uiStartPos = m_sInLine.find_first_not_of( " \t" );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passDynamicIncludeLine( uiStartPos, sIdent, m_mUnknowns[sPrefix] ) )
        {
            return false;
        }
    }
    return true;
}

bool projectGenerator::passDLibUnknown( )
{
    //Find the dynamic identifier
    uint uiStartPos = m_sInLine.find( "$(" );
    uint uiEndPos = m_sInLine.find( ')', uiStartPos );
    string sPrefix = m_sInLine.substr( 0, uiStartPos ) + "yes";
    uiStartPos += 2; //Skip the $(
    string sIdent = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
    //Find the included obj
    uiStartPos = m_sInLine.find_first_not_of( "+= \t", uiEndPos+1 );
    if( !passDynamicIncludeLine( uiStartPos, sIdent, m_mUnknowns[sPrefix] ) )
    {
        return false;
    }
    //Check if this is a multi line declaration
    while( m_sInLine.back() == '\\' )
    {
        //Get the next line
        getline( m_ifInputFile, m_sInLine );
        //Remove the whitespace
        uiStartPos = m_sInLine.find_first_not_of( " \t" );
        if( uiStartPos == string::npos )
        {
            break;
        }
        if( !passDynamicIncludeLine( uiStartPos, sIdent, m_mUnknowns[sPrefix] ) )
        {
            return false;
        }
    }
    return true;
}

bool projectGenerator::passMake( )
{
    cout << "  Generating from Makefile (" << m_sProjectDir << ")..." << endl;
    //Open the input Makefile
    string sMakeFile = m_sProjectDir + "/MakeFile";
    m_ifInputFile.open( sMakeFile );
    if( m_ifInputFile.is_open( ) )
    {
        //Read each line in the MakeFile
        while( getline( m_ifInputFile, m_sInLine ) )
        {
            //Check what information is included in the current line
            if( m_sInLine.substr(0, 4).compare("OBJS") == 0 )
            {
                //Found some c includes
                if( m_sInLine.at(4) == '-' )
                {
                    //Found some dynamic c includes
                    if( !passDCInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
                else
                {
                    //Found some static c includes
                    if( !passCInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
            }
            else if( m_sInLine.substr(0, 9).compare("YASM-OBJS") == 0 )
            {
                //Found some YASM includes
                if( m_sInLine.at(9) == '-' )
                {
                    //Found some dynamic YASM includes
                    if( !passDYASMInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
                else
                {
                    //Found some static YASM includes
                    if( !passYASMInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
            }
            else if( m_sInLine.substr(0, 8).compare("MMX-OBJS") == 0 )
            {
                //Found some YASM includes
                if( m_sInLine.at(8) == '-' )
                {
                    //Found some dynamic YASM includes
                    if( !passDMMXInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
                else
                {
                    //Found some static YASM includes
                    if( !passMMXInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
            }
            else if( m_sInLine.substr(0, 7).compare("HEADERS") == 0 )
            {
                //Found some static headers
                if( m_sInLine.at(7) == '-' )
                {
                    //Found some dynamic headers
                    if( !passDHInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
                else
                {
                    //Found some static headers
                    if( !passHInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
            }
            else if( m_sInLine.substr(0, 6).compare("FFLIBS") == 0 )
            {
                //Found some libs
                if( m_sInLine.at(6) == '-' )
                {
                    //Found some dynamic libs
                    if( !passDLibInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
                else
                {
                    //Found some static libs
                    if( !passLibInclude( ) )
                    {
                        m_ifInputFile.close( );
                        return false;
                    }
                }
            }
            else if( m_sInLine.find("-OBJS-$") != string::npos )
            {
                //Found unknown
                if( !passDUnknown( ) )
                {
                    m_ifInputFile.close( );
                    return false;
                }
            }
            else if(m_sInLine.find("LIBS-$") != string::npos )
            {
                //Found Lib unknown
                if( !passDLibUnknown( ) )
                {
                    m_ifInputFile.close( );
                    return false;
                }
            }
        }
        m_ifInputFile.close( );
        return true;
    }
    cout << "  Error: could not open open MakeFile (" << sMakeFile << ")" << endl;
    return false;
}

bool projectGenerator::passToolchain( string & sToolchain )
{
    if( m_ConfigHelper.m_sToolchain.compare( "msvc" ) == 0 )
    {
        //Check for the existence of complient msvc compiler
        if( GetEnvironmentVariable( "VS150COMNTOOLS", NULL, 0 ) )
        {
            //??? Future release
            sToolchain = "v150";
        }
        if( GetEnvironmentVariable("VS140COMNTOOLS", NULL, 0 ) )
        {
            sToolchain = "v140";
        }
        else if( GetEnvironmentVariable( "VS120COMNTOOLS", NULL, 0 ) )
        {
            sToolchain = "v120";
        }
        else
        {
            cout << "  Error: Failed finding valid MSVC compiler (Requires VS2013 or higher)." << endl;
            return false;
        }
    }
    else
    {
        //Check for the existence of Intel compiler
        if( GetEnvironmentVariable( "ICPP_COMPILER16", NULL, 0 ) )
        {
            //??? Future release
            sToolchain = "Intel C++ Compiler XE 16.0";
        }
        else if( GetEnvironmentVariable( "ICPP_COMPILER15", NULL, 0 ) )
        {
            sToolchain = "Intel C++ Compiler XE 15.0";
        }
        else if( GetEnvironmentVariable( "ICPP_COMPILER14", NULL, 0 ) )
        {
            sToolchain = "Intel C++ Compiler XE 14.0";
        }
        else if( GetEnvironmentVariable( "ICPP_COMPILER13", NULL, 0 ) )
        {
            sToolchain = "Intel C++ Compiler XE 13.0";
        }
        else
        {
            cout << "  Error: Failed finding valid Intel compiler." << endl;
            return false;
        }
    }
    return true;
}

bool projectGenerator::findFile( const string & sFileName, string & sRetFileName )
{
    WIN32_FIND_DATA SearchFile;
    HANDLE SearchHandle=FindFirstFile( sFileName.c_str( ), &SearchFile );
    if( SearchHandle != INVALID_HANDLE_VALUE )
    {
        //Update the return filename
        sRetFileName = SearchFile.cFileName;
        FindClose( SearchHandle );
        return true;
    }
    return false;
}

bool projectGenerator::findFiles( const string & sFileSearch, vector<string> & vRetFiles )
{
    WIN32_FIND_DATA SearchFile;
    uint uiStartSize = vRetFiles.size( );
    uint uiPos = sFileSearch.rfind( '\\' );
    if( sFileSearch.rfind( '/' ) != string::npos )
    {
        cout << "Fuck" << endl;
        return false;
    }
    string sPath;
    string sSearchTerm = sFileSearch;
    if( uiPos != string::npos )
    {
        ++uiPos;
        sPath = sFileSearch.substr( 0, uiPos );
        sSearchTerm = sFileSearch.substr( uiPos );
    }
    HANDLE SearchHandle=FindFirstFile( sFileSearch.c_str( ), &SearchFile );
    if( SearchHandle != INVALID_HANDLE_VALUE )
    {
        //Update the return filename list
        vRetFiles.push_back( sPath + SearchFile.cFileName );
        while( FindNextFile( SearchHandle, &SearchFile ) != 0 )
        {
            vRetFiles.push_back( sPath + SearchFile.cFileName );
        }
        FindClose( SearchHandle );
    }
    //Search all sub directories as well
    string sSearch = sPath + "*";
    SearchHandle = FindFirstFile( sSearch.c_str( ), &SearchFile );
    if( SearchHandle != INVALID_HANDLE_VALUE )
    {
        BOOL bCont = TRUE;
        while( bCont == TRUE )
        {
            if( SearchFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            {
                // this is a directory
                if( strcmp( SearchFile.cFileName, "." ) != 0 && strcmp( SearchFile.cFileName, ".." ) != 0 )
                {
                    string sNewPath = sPath + SearchFile.cFileName + '\\' + sSearchTerm;
                    findFiles( sNewPath, vRetFiles );
                }
            }
            bCont = FindNextFile( SearchHandle, &SearchFile );
        }
        FindClose( SearchHandle );
    }
    return ( vRetFiles.size( ) - uiStartSize ) > 0;
}

bool projectGenerator::findSourceFile( const string & sFile, const string & sExtension, string & sRetFileName )
{
    string sFileName;
    sRetFileName = m_sProjectDir + sFile + sExtension;
    return findFile( sRetFileName, sFileName );
}

bool projectGenerator::findSourceFiles( const string & sFile, const string & sExtension, vector<string> & vRetFiles )
{
    string sFileName = m_sProjectDir + sFile + sExtension;
    return findFiles( sFileName, vRetFiles );
}

bool projectGenerator::copyFile( const string & sSourceFile, const string & sDestinationFile )
{
    ifstream ifSource( sSourceFile, ios::binary );
    if( !ifSource.is_open() )
    {
        return false;
    }
    ofstream ifDest( sDestinationFile, ios::binary );
    if( !ifDest.is_open() )
    {
        return false;
    }
    ifDest << ifSource.rdbuf();
    ifSource.close();
    ifDest.close();
    return true;
}

void projectGenerator::deleteFile( const string & sDestinationFile )
{
    DeleteFile( sDestinationFile.c_str() );
}

void projectGenerator::deleteFolder( const string & sDestinationFolder )
{
    string delFolder = sDestinationFolder + '\0';
    SHFILEOPSTRUCT file_op = { NULL, FO_DELETE, delFolder.c_str( ), "", FOF_NO_UI, false, 0, "" };
    SHFileOperation( &file_op );
}
