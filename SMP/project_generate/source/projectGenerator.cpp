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

#include <iostream>
#include <algorithm>
#include <Windows.h>

bool projectGenerator::passAllMake( )
{
    //Initialise internal values
    configGenerator::DefaultValuesList Unneeded;
    m_ConfigHelper.buildReplaceValues(m_ReplaceValues, Unneeded);

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
            m_mReplaceIncludes.clear();
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

    //Check all files are correctly located
    if( !checkProjectFiles(sProjectName) )
        return false;

    //We now have complete list of all the files the we need
    cout << "  Generating project file (" << sProjectName << ")..." << endl;

    //Open the input temp project file
    string sProjectFile;
    loadFromFile("../templates/template_in.vcxproj", sProjectFile);

    //Open the input temp project file filters
    string sFiltersFile;
    loadFromFile("../templates/template_in.vcxproj.filters", sFiltersFile);

    //Replace all template tag arguments
    outputTemplateTags(sProjectName, sProjectFile, sFiltersFile);

    //Add all project source files
    outputSourceFiles(sProjectName, sProjectFile, sFiltersFile);

    //Add the build events
    outputBuildEvents(sProjectName, sProjectFile);

    //Add YASM requirements
    outputYASMTools(sProjectFile);

    //Add the dependency libraries
    if (!outputDependencyLibs(sProjectName, sProjectFile)) {
        return false;
    }

    //Get dependency directories
    StaticList vIncludeDirs;
    StaticList vLib32Dirs;
    StaticList vLib64Dirs;
    buildDependencyDirs(sProjectName, vIncludeDirs, vLib32Dirs, vLib64Dirs);

    //Add additional includes to include list
    outputIncludeDirs(vIncludeDirs, sProjectFile);

    //Add additional lib includes to include list
    outputLibDirs(vLib32Dirs, vLib64Dirs, sProjectFile);

    //Write output project
    string sOutProjectFile = "../../" + sProjectName + ".vcxproj";
    if (!writeToFile(sOutProjectFile, sProjectFile)) {
        return false;
    }

    //Write output filters
    string sOutFiltersFile = "../../" + sProjectName + ".vcxproj.filters";
    if (!writeToFile(sOutFiltersFile, sFiltersFile)) {
        return false;
    }

    //Open the exports files and get export names
    cout << "  Generating project exports file (" << sProjectName << ")..." << endl;
    if (!outputProjectExports(sProjectName, vIncludeDirs)) {
        return false;
    }

    return true;
}

bool projectGenerator::outputProgramProject(const string& sProjectName, const string& sDestinationFile, const string& sDestinationFilterFile)
{
    //Open the template program
    string sProgramFile;
    loadFromFile("../templates/templateprogram_in.vcxproj", sProgramFile);

    //Open the template program filters
    string sProgramFiltersFile;
    loadFromFile("../templates/templateprogram_in.vcxproj.filters", sProgramFiltersFile);

    //Replace all template tag arguments
    outputTemplateTags(sProjectName, sProgramFile, sProgramFiltersFile);

    //Pass makefile for program
    passProgramMake(sProjectName);

    //Check all files are correctly located
    if (!checkProjectFiles(sProjectName))
        return false;

    //Add all project source files
    outputSourceFiles(sProjectName, sProgramFile, sProgramFiltersFile);

    //Add the build events
    outputBuildEvents(sProjectName, sProgramFile);

    //Add YASM requirements (currently there are not any but just in case)
    outputYASMTools(sProgramFile);

    //Add the dependency libraries
    if (!outputDependencyLibs(sProjectName, sProgramFile, true)) {
        return false;
    }

    //Get dependency directories
    StaticList vIncludeDirs;
    StaticList vLib32Dirs;
    StaticList vLib64Dirs;
    buildDependencyDirs(sProjectName, vIncludeDirs, vLib32Dirs, vLib64Dirs);

    //Add additional includes to include list
    outputIncludeDirs(vIncludeDirs, sProgramFile);

    //Add additional lib includes to include list
    outputLibDirs(vLib32Dirs, vLib64Dirs, sProgramFile);

    //Write program file
    if (!writeToFile(sDestinationFile, sProgramFile)) {
        return false;
    }

    //Write output filters
    if (!writeToFile(sDestinationFilterFile, sProgramFiltersFile)) {
        return false;
    }

    //Reset all internal values
    m_sInLine.clear();
    m_vIncludes.clear();
    m_vCPPIncludes.clear();
    m_vCIncludes.clear();
    m_vYASMIncludes.clear();
    m_vHIncludes.clear();
    m_vLibs.clear();
    m_mUnknowns.clear();

    return true;
}

bool projectGenerator::outputSolution()
{
    cout << "  Generating solution file..." << endl;
    m_sProjectDir = "..\\..\\..\\";
    //Open the input temp project file
    string sSolutionFile;
    loadFromFile("../templates/template_in.sln", sSolutionFile);

    map<string, string> mKeys;
    buildProjectGUIDs(mKeys);
    string sSolutionKey = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942";

    vector<string> vAddedKeys;

    const string sProject = "\nProject(\"{";
    const string sProject2 = "}\") = \"";
    const string sProject3 = "\", \"";
    const string sProject4 = ".vcxproj\", \"{";
    const string sProjectEnd = "}\"";
    const string sProjectClose = "\nEndProject";

    const string sDepend = "\n	ProjectSection(ProjectDependencies) = postProject";
    const string sDependClose = "\n	EndProjectSection";
    const string sSubDepend =	"\n		{";
    const string sSubDepend2 = "} = {";
    const string sSubDependEnd = "}";

    //Find the start of the file
    const string sFileStart = "Project";
    uint uiPos = sSolutionFile.find( sFileStart ) - 1;

    map<string,StaticList>::iterator mitLibraries = m_mProjectLibs.begin();
    while( mitLibraries != m_mProjectLibs.end( ) )
    {
        //Check if this library has a known key (to lazy to auto generate at this time)
        if (mKeys.find(mitLibraries->first) == mKeys.end()) {
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
        sProjectAdd += mKeys[mitLibraries->first];
        sProjectAdd += sProjectEnd;

        //Add the key to the used key list
        vAddedKeys.push_back(mKeys[mitLibraries->first]);

        //Add the dependencies
        if( mitLibraries->second.size() > 0 )
        {
            sProjectAdd += sDepend;
            for( StaticList::iterator vitIt=mitLibraries->second.begin(); vitIt<mitLibraries->second.end(); vitIt++ )
            {
                //Check if this library has a known key
                if (mKeys.find(*vitIt) == mKeys.end()) {
                    cout << "  Error: Unknown library dependency. Could not determine solution key (" << *vitIt << ")" << endl;
                    return false;
                }
                sProjectAdd += sSubDepend;
                sProjectAdd += mKeys[*vitIt];
                sProjectAdd += sSubDepend2;
                sProjectAdd += mKeys[*vitIt];
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
            //Create project files for program
            outputProgramProject(mitPrograms->first, sDestinationFile, sDestinationFilterFile);

            //Add the program to the solution
            sProjectAdd += sProject;
            sProjectAdd += sSolutionKey;
            sProjectAdd += sProject2;
            sProjectAdd += mitPrograms->first;
            sProjectAdd += sProject3;
            sProjectAdd += mitPrograms->first;
            sProjectAdd += sProject4;
            sProjectAdd += mKeys[mitPrograms->first];
            sProjectAdd += sProjectEnd;

            //Add the key to the used key list
            vAddedPrograms.push_back(mKeys[mitPrograms->first]);

            //Add the dependencies
            sProjectAdd += sDepend;
            StaticList::iterator vitLibs = m_mProjectLibs[mitPrograms->first].begin();
            while (vitLibs != m_mProjectLibs[mitPrograms->first].end()) {
                //Add all project libraries as dependencies (except avresample with ffmpeg)
                if (!m_ConfigHelper.m_bLibav && vitLibs->compare("libavresample") != 0) {
                    sProjectAdd += sSubDepend;
                    sProjectAdd += mKeys[*vitLibs];
                    sProjectAdd += sSubDepend2;
                    sProjectAdd += mKeys[*vitLibs];
                    sProjectAdd += sSubDependEnd;
                }
                //next
                ++vitLibs;
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

    //Write output solution
    string sProjectName = m_ConfigHelper.m_sProjectName;
    transform( sProjectName.begin( ), sProjectName.end( ), sProjectName.begin( ), ::tolower );
    const string sOutSolutionFile = "../../" + sProjectName + ".sln";
    if (!writeToFile(sOutSolutionFile, sSolutionFile)) {
        return false;
    }

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
    uint uiStartPos = m_sInLine.find_first_not_of( " +=:", uiILength );
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
                //Check if the config option is for a reserved type
                if (m_ReplaceValues.find(sIdent) != m_ReplaceValues.end()) {
                    m_mReplaceIncludes[sTag].push_back(sIdent);
                    //cout << "  Found Dynamic Replace: '" << sTag << "', '" << sIdent << "'" << endl;
                } else {
                    vIncludes.push_back(sTag);
                    //cout << "  Found Dynamic: '" << sTag << "', '" << sIdent << "'" << endl;
                }
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
    uint uiEndPos = m_sInLine.find( ')', uiStartPos);
    string sIdent = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
    //Find the included obj
    uiStartPos = m_sInLine.find_first_not_of( "+=: \t", uiEndPos+1 );
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

bool projectGenerator::passHInclude( uint uiCutPos )
{
    return passStaticInclude( uiCutPos, m_vHIncludes );
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
                //Found some headers
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
            else if( m_sInLine.find( "BUILT_HEADERS" ) == 0 )
            {
                //Found some static built headers
                if( !passHInclude( 13 ) )
                {
                    m_ifInputFile.close( );
                    return false;
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
            else if( m_sInLine.find("LIBS-$") != string::npos )
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

bool projectGenerator::passProgramMake(const string & sProjectName)
{
    cout << "  Generating from Makefile (" << m_sProjectDir << ") for project " << sProjectName << "..." << endl;
    //Open the input Makefile
    string sMakeFile = m_sProjectDir + "/MakeFile";
    m_ifInputFile.open(sMakeFile);
    if (m_ifInputFile.is_open()) {
        string sObjTag = "OBJS-" + sProjectName;
        uint uiFindPos;
        //Read each line in the MakeFile
        while (getline(m_ifInputFile, m_sInLine)) {
            //Check what information is included in the current line
            if (m_sInLine.substr(0, sObjTag.length()).compare(sObjTag) == 0) {
                //Cut the line so it can be used by default passers
                m_sInLine = m_sInLine.substr(sObjTag.length() - 4);
                if (m_sInLine.at(4) == '-') {
                    //Found some dynamic c includes
                    if (!passDCInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                } else {
                    //Found some static c includes
                    if (!passCInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                }
            } else if (m_sInLine.substr(0, 6).compare("FFLIBS") == 0) {
                //Found some libs
                if (m_sInLine.at(6) == '-') {
                    //Found some dynamic libs
                    if (!passDLibInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                } else {
                    //Found some static libs
                    if (!passLibInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                }
            } else if ((uiFindPos = m_sInLine.find("eval OBJS-$(prog)")) != string::npos) {
                m_sInLine = m_sInLine.substr(uiFindPos + 13);
                if (m_sInLine.at(4) == '-') {
                    //Found some dynamic c includes
                    if (!passDCInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                } else {
                    //Found some static c includes
                    if (!passCInclude()) {
                        m_ifInputFile.close();
                        return false;
                    }
                }
            }
        }
        m_ifInputFile.close();

        //Program always include a file named after themselves
        m_vIncludes.push_back(sProjectName);
        return true;
    }
    cout << "  Error: could not open open MakeFile (.\\MakeFile)" << endl;
    return false;
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
    if( !findFile( sRetFileName, sFileName ) )
    {
        // Check if this is a built file
        uint uiSPos = m_sProjectDir.rfind( '\\', m_sProjectDir.length( ) - 2 );
        string sProjectName = m_sProjectDir.substr( uiSPos );
        sRetFileName = m_sProjectDir.substr( 0, uiSPos + 1 ) + "SMP" + sProjectName + sFile + sExtension;
        return findFile( sRetFileName, sFileName );
    }
    return true;
}

bool projectGenerator::findSourceFiles( const string & sFile, const string & sExtension, vector<string> & vRetFiles )
{
    string sFileName = m_sProjectDir + sFile + sExtension;
    return findFiles( sFileName, vRetFiles );
}

void projectGenerator::makeFileProjectRelative( const string & sFileName, string & sRetFileName)
{
    sRetFileName = sFileName.substr( 6 ); //Remove the preceding ..\..\ so the file is up 2 directories
    if( sRetFileName.find( "..\\SMP" ) == 0 )
    {
        sRetFileName = sRetFileName.substr( 7 ); //Remove the preceding ..\SMP
    }
}

bool projectGenerator::checkProjectFiles(const string& sProjectName)
{
    //Check that all headers are correct
    for (StaticList::iterator itIt = m_vHIncludes.begin(); itIt != m_vHIncludes.end(); itIt++) {
        string sRetFileName;
        if (!findSourceFile(*itIt, ".h", sRetFileName)) {
            cout << "  Error: could not find input header file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        makeFileProjectRelative(sRetFileName, *itIt);
    }

    //Check that all C Source are correct
    for (StaticList::iterator itIt = m_vCIncludes.begin(); itIt != m_vCIncludes.end(); itIt++) {
        string sRetFileName;
        if (!findSourceFile(*itIt, ".c", sRetFileName)) {
            cout << "  Error: could not find input C source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        makeFileProjectRelative(sRetFileName, *itIt);
    }

    //Check that all CPP Source are correct
    for (StaticList::iterator itIt = m_vCPPIncludes.begin(); itIt != m_vCPPIncludes.end(); itIt++) {
        string sRetFileName;
        if (!findSourceFile(*itIt, ".cpp", sRetFileName)) {
            cout << "  Error: could not find input C++ source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        makeFileProjectRelative(sRetFileName, *itIt);
    }

    //Check that all ASM Source are correct
    for (StaticList::iterator itIt = m_vYASMIncludes.begin(); itIt != m_vYASMIncludes.end(); itIt++) {
        string sRetFileName;
        if (!findSourceFile(*itIt, ".asm", sRetFileName)) {
            cout << "  Error: could not find input ASM source file for object (" << *itIt << ")" << endl;
            return false;
        }
        //Update the entry with the found file with complete path
        makeFileProjectRelative(sRetFileName, *itIt);
    }

    //Check the output Unknown Includes and find there corresponding file
    if (!findProjectFiles(m_vIncludes, m_vCIncludes, m_vCPPIncludes, m_vYASMIncludes, m_vHIncludes)) {
        return false;
    }

    //Delete any previously existing replace files
    vector<string> vExistingRepFiles;
    findFiles("../../" + sProjectName + "/*.c", vExistingRepFiles);
    findFiles("../../" + sProjectName + "/*.cpp", vExistingRepFiles);
    findFiles("../../" + sProjectName + "/*.asm", vExistingRepFiles);
    for (vector<string>::iterator itIt = vExistingRepFiles.begin(); itIt < vExistingRepFiles.end(); itIt++) {
        deleteFile(*itIt);
    }
    //Check all source files associated with replaced config values
    StaticList vReplaceIncludes, vReplaceCPPIncludes, vReplaceCIncludes, vReplaceYASMIncludes;
    for (UnknownList::iterator itIt = m_mReplaceIncludes.begin(); itIt != m_mReplaceIncludes.end(); itIt++) {
        vReplaceIncludes.push_back(itIt->first);
    }
    if (!findProjectFiles(vReplaceIncludes, vReplaceCIncludes, vReplaceCPPIncludes, vReplaceYASMIncludes, m_vHIncludes)) {
        return false;
    } else {
        //Need to create local files for any replace objects
        if (!createReplaceFiles(vReplaceCIncludes, m_vCIncludes, sProjectName)) {
            return false;
        }
        if (!createReplaceFiles(vReplaceCPPIncludes, m_vCPPIncludes, sProjectName)) {
            return false;
        }
        if (!createReplaceFiles(vReplaceYASMIncludes, m_vYASMIncludes, sProjectName)) {
            return false;
        }
    }

    return true;
}

bool projectGenerator::createReplaceFiles(const StaticList& vReplaceIncludes, StaticList& vExistingIncludes, const string& sProjectName)
{
    for (StaticList::const_iterator itIt = vReplaceIncludes.cbegin(); itIt != vReplaceIncludes.cend(); itIt++) {
        //Check hasnt already been included as a fixed object
        if (find(vExistingIncludes.begin(), vExistingIncludes.end(), *itIt) != vExistingIncludes.end()) {
            //skip this item
            continue;
        }
        //Convert file to format required to search ReplaceIncludes
        uint uiExtPos = itIt->rfind('.');
        uint uiCutPos = itIt->rfind('\\') + 1;
        string sFilename = itIt->substr(uiCutPos, uiExtPos - uiCutPos);
        string sExtension = itIt->substr(uiExtPos);
        //Get the files dynamic config requirement
        string sIdents;
        for (StaticList::iterator itIdents = m_mReplaceIncludes[sFilename].begin(); itIdents < m_mReplaceIncludes[sFilename].end(); itIdents++) {
            sIdents += *itIdents;
            if ((itIdents + 1) < m_mReplaceIncludes[sFilename].end()) {
                sIdents += " || ";
            }
        }
        //Create new file to wrap input object
        string sPrettyFile = "../" + *itIt;
        replace(sPrettyFile.begin(), sPrettyFile.end(), '\\', '/');
        string sNewFile = getCopywriteHeader(sFilename + sExtension + " file wrapper for " + sProjectName);
        sNewFile += "\n\
\n\
#include \"config.h\"\n\
#if " + sIdents + "\n\
#   include \"" + sPrettyFile + "\"\n\
#endif";
        //Write output project
        if (!makeDirectory("../../" + sProjectName)) {
            cout << "  Error: Failed creating local " + sProjectName + " directory" << endl;
            return false;
        }
        string sOutFile = "../../" + sProjectName + "/" + sFilename + "_wrap" + sExtension;
        if (!writeToFile(sOutFile, sNewFile)) {
            return false;
        }
        //Add the new file to list of objects
        makeFileProjectRelative(sOutFile, sOutFile);
        vExistingIncludes.push_back(sOutFile);
    }
    return true;
}

bool projectGenerator::findProjectFiles(const StaticList& vIncludes, StaticList& vCIncludes, StaticList& vCPPIncludes, StaticList& vASMIncludes, StaticList& vHIncludes)
{
    for (StaticList::const_iterator itIt = vIncludes.cbegin(); itIt != vIncludes.cend(); itIt++) {
        string sRetFileName;
        if (findSourceFile(*itIt, ".c", sRetFileName)) {
            //Found a C File to include
            if (find(vCIncludes.begin(), vCIncludes.end(), sRetFileName) != vCIncludes.end()) {
                //skip this item
                continue;
            }
            makeFileProjectRelative(sRetFileName, sRetFileName);
            vCIncludes.push_back(sRetFileName);
        } else if (findSourceFile(*itIt, ".cpp", sRetFileName)) {
            //Found a C++ File to include
            if (find(vCPPIncludes.begin(), vCPPIncludes.end(), sRetFileName) != vCPPIncludes.end()) {
                //skip this item
                continue;
            }
            makeFileProjectRelative(sRetFileName, sRetFileName);
            vCPPIncludes.push_back(sRetFileName);
        } else if (findSourceFile(*itIt, ".asm", sRetFileName)) {
            //Found a ASM File to include
            if (find(vASMIncludes.begin(), vASMIncludes.end(), sRetFileName) != vASMIncludes.end()) {
                //skip this item
                continue;
            }
            makeFileProjectRelative(sRetFileName, sRetFileName);
            vASMIncludes.push_back(sRetFileName);
        } else if (findSourceFile(*itIt, ".h", sRetFileName)) {
            //Found a H File to include
            if (find(vHIncludes.begin(), vHIncludes.end(), sRetFileName) != vHIncludes.end()) {
                //skip this item
                continue;
            }
            makeFileProjectRelative(sRetFileName, sRetFileName);
            vHIncludes.push_back(sRetFileName);
        } else {
            cout << "  Error: Could not find valid source file for object (" << *itIt << ")" << endl;
            return false;
        }
    }
    return true;
}

void projectGenerator::outputTemplateTags(const string& sProjectName, string & sProjectTemplate, string& sFiltersTemplate)
{
    //Change all occurance of template_in with project name
    const string sFFSearchTag = "template_in";
    uint uiFindPos = sProjectTemplate.find(sFFSearchTag);
    while (uiFindPos != string::npos) {
        //Replace
        sProjectTemplate.replace(uiFindPos, sFFSearchTag.length(), sProjectName);
        //Get next
        uiFindPos = sProjectTemplate.find(sFFSearchTag, uiFindPos + 1);
    }
    uint uiFindPosFilt = sFiltersTemplate.find(sFFSearchTag);
    while (uiFindPosFilt != string::npos) {
        //Replace
        sFiltersTemplate.replace(uiFindPosFilt, sFFSearchTag.length(), sProjectName);
        //Get next
        uiFindPosFilt = sFiltersTemplate.find(sFFSearchTag, uiFindPosFilt + 1);
    }

    //Change all occurance of template_shin with short project name
    const string sFFShortSearchTag = "template_shin";
    uiFindPos = sProjectTemplate.find(sFFShortSearchTag);
    string sProjectNameShort = sProjectName.substr(3); //The full name minus the lib prefix
    while (uiFindPos != string::npos) {
        //Replace
        sProjectTemplate.replace(uiFindPos, sFFShortSearchTag.length(), sProjectNameShort);
        //Get next
        uiFindPos = sProjectTemplate.find(sFFShortSearchTag, uiFindPos + 1);
    }
    uiFindPosFilt = sFiltersTemplate.find(sFFShortSearchTag);
    while (uiFindPosFilt != string::npos) {
        //Replace
        sFiltersTemplate.replace(uiFindPosFilt, sFFShortSearchTag.length(), sProjectNameShort);
        //Get next
        uiFindPosFilt = sFiltersTemplate.find(sFFShortSearchTag, uiFindPosFilt + 1);
    }

    //Change all occurance of template_platform with specified project toolchain
    string sToolchain = "<PlatformToolset Condition=\"'$(VisualStudioVersion)'=='12.0'\">v120</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='14.0'\">v140</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(VisualStudioVersion)'=='15.0'\">v150</PlatformToolset>";
    if (m_ConfigHelper.m_sToolchain.compare("icl") == 0) {
        sToolchain += "\n    <PlatformToolset Condition=\"'$(ICPP_COMPILER13)'!=''\">Intel C++ Compiler XE 13.0</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(ICPP_COMPILER14)'!=''\">Intel C++ Compiler XE 14.0</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(ICPP_COMPILER15)'!=''\">Intel C++ Compiler XE 15.0</PlatformToolset>\n\
    <PlatformToolset Condition=\"'$(ICPP_COMPILER16)'!=''\">Intel C++ Compiler 16.0</PlatformToolset>";
    }

    const string sPlatformSearch = "<PlatformToolset>template_platform</PlatformToolset>";
    uiFindPos = sProjectTemplate.find(sPlatformSearch);
    while (uiFindPos != string::npos) {
        //Replace
        sProjectTemplate.replace(uiFindPos, sPlatformSearch.length(), sToolchain);
        //Get next
        uiFindPos = sProjectTemplate.find(sPlatformSearch, uiFindPos + sPlatformSearch.length());
    }

    //Set the project key
    string sGUID = "<ProjectGuid>{";
    uiFindPos = sProjectTemplate.find(sGUID);
    if (uiFindPos != string::npos) {
        map<string, string> mKeys;
        buildProjectGUIDs(mKeys);
        uiFindPos += sGUID.length();
        sProjectTemplate.replace(uiFindPos, mKeys[sProjectName].length(), mKeys[sProjectName]);
    }
}

void projectGenerator::outputSourceFileType(StaticList& vFileList, const string& sType, const string& sFilterType, string & sProjectTemplate, string & sFilterTemplate, StaticList& vFoundObjects, set<string>& vFoundFilters, bool bCheckExisting)
{
    //Declare constant strings used in output files
    const string sItemGroup = "\n  <ItemGroup>";
    const string sItemGroupEnd = "\n  </ItemGroup>";
    const string sIncludeClose = "\">";
    const string sIncludeEnd = "\" />";
    const string sTypeInclude = "\n    <" + sType + " Include=\"";
    const string sTypeIncludeEnd = "\n    </" + sType + ">";
    const string sIncludeObject = "\n      <ObjectFileName>$(IntDir)\\";
    const string sIncludeObjectClose = ".obj</ObjectFileName>";
    const string sFilterSource = "\n      <Filter>" + sFilterType + " Files";
    const string sSource = sFilterType + " Files";
    const string sFilterEnd = "</Filter>";

    if (vFileList.size() > 0) {
        string sTypeFiles = sItemGroup;
        string sTypeFilesFilt = sItemGroup;

        for (StaticList::iterator vitInclude = vFileList.begin(); vitInclude < vFileList.end(); vitInclude++) {
            //Output objects
            sTypeFiles += sTypeInclude;
            sTypeFilesFilt += sTypeInclude;

            //Add the fileName
            replace(vitInclude->begin(), vitInclude->end(), '/', '\\');
            sTypeFiles += *vitInclude;
            sTypeFilesFilt += *vitInclude;

            //Get object name without path or extension
            uint uiPos = vitInclude->rfind('\\') + 1;
            string sObjectName = vitInclude->substr(uiPos);
            uint uiPos2 = sObjectName.rfind('.');
            sObjectName.resize(uiPos2);

            //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
            uiPos = vitInclude->rfind("..\\");
            uiPos = (uiPos == string::npos) ? 0 : uiPos + 3;
            sTypeFilesFilt += sIncludeClose;
            if (bCheckExisting) {
                if (find(vFoundObjects.begin(), vFoundObjects.end(), sObjectName) != vFoundObjects.end()) {
                    sObjectName = vitInclude->substr(uiPos);
                    replace(sObjectName.begin(), sObjectName.end(), '\\', '_');
                    //Replace the extension with obj
                    uiPos2 = sObjectName.rfind('.');
                    sObjectName.resize(uiPos2);
                    sTypeFiles += sIncludeClose;
                    sTypeFiles += sIncludeObject;
                    sTypeFiles += sObjectName;
                    sTypeFiles += sIncludeObjectClose;
                    sTypeFiles += sTypeIncludeEnd;
                } else {
                    vFoundObjects.push_back(sObjectName);
                    //Close the current item
                    sTypeFiles += sIncludeEnd;
                }
            } else {
                vFoundObjects.push_back(sObjectName);
                //Close the current item
                sTypeFiles += sIncludeEnd;
            }

            //Add the filters Filter
            sTypeFilesFilt += sFilterSource;
            uint uiFolderLength = vitInclude->rfind('\\') - uiPos;
            if ((int)uiFolderLength != -1) {
                string sFolderName = vitInclude->substr(uiPos, uiFolderLength);
                sFolderName = "\\" + sFolderName;
                vFoundFilters.insert(sSource + sFolderName);
                sTypeFilesFilt += sFolderName;
            }
            sTypeFilesFilt += sFilterEnd;
            sTypeFilesFilt += sTypeIncludeEnd;
        }
        sTypeFiles += sItemGroupEnd;
        sTypeFilesFilt += sItemGroupEnd;

        //After </ItemGroup> add the item groups for each of the include types
        uint uiFindPos = sProjectTemplate.rfind(sItemGroupEnd);
        uiFindPos += sItemGroupEnd.length();
        uint uiFindPosFilt = sFilterTemplate.rfind(sItemGroupEnd);
        uiFindPosFilt += sItemGroupEnd.length();

        //Insert into output file
        sProjectTemplate.insert(uiFindPos, sTypeFiles);
        uiFindPos += sTypeFiles.length();
        sFilterTemplate.insert(uiFindPosFilt, sTypeFilesFilt);
        uiFindPosFilt += sTypeFilesFilt.length();
    }
}

void projectGenerator::outputSourceFiles(const string & sProjectName, string & sProjectTemplate, string & sFilterTemplate)
{
    set<string> vFoundFilters;
    StaticList vFoundObjects;

    //Output ASM files in specific item group (must go first as asm does not allow for custom obj filename)
    if (m_ConfigHelper.getConfigOptionPrefixed("HAVE_YASM")->m_sValue.compare("1") == 0) {
        outputSourceFileType(m_vYASMIncludes, "YASM", "Source", sProjectTemplate, sFilterTemplate, vFoundObjects, vFoundFilters, false);
    }

    //Output C files
    outputSourceFileType(m_vCIncludes, "ClCompile", "Source", sProjectTemplate, sFilterTemplate, vFoundObjects, vFoundFilters, true);

    //Output C++ files
    outputSourceFileType(m_vCPPIncludes, "ClCompile", "Source", sProjectTemplate, sFilterTemplate, vFoundObjects, vFoundFilters, true);

    //Output header files in new item group
    outputSourceFileType(m_vHIncludes, "ClInclude", "Header", sProjectTemplate, sFilterTemplate, vFoundObjects, vFoundFilters, false);

    //Add any additional Filters to filters file
    const string sItemGroupEnd = "\n  </ItemGroup>";
    const string sFilterAdd = "\n    <Filter Include=\"";
    const string sFilterAdd2 = "\">\n\
      <UniqueIdentifier>{";
    const string sFilterAddClose = "}</UniqueIdentifier>\n\
    </Filter>";
    const string asFilterKeys[] = {"cac6df1e-4a60-495c-8daa-5707dc1216ff", "9fee14b2-1b77-463a-bd6b-60efdcf8850f",
        "bf017c32-250d-47da-b7e6-d5a5091cb1e6", "fd9e10e9-18f6-437d-b5d7-17290540c8b8", "f026e68e-ff14-4bf4-8758-6384ac7bcfaf",
        "a2d068fe-f5d5-4b6f-95d4-f15631533341", "8a4a673d-2aba-4d8d-a18e-dab035e5c446", "0dcfb38d-54ca-4ceb-b383-4662f006eca9",
        "57bf1423-fb68-441f-b5c1-f41e6ae5fa9c"};

    //get start position in file
    uint uiFindPosFilt = sFilterTemplate.find(sItemGroupEnd);
    set<string>::iterator sitIt = vFoundFilters.begin();
    uint uiCurrentKey = 0;
    string sAddFilters;
    while (sitIt != vFoundFilters.end()) {
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
    sFilterTemplate.insert(uiFindPosFilt, sAddFilters);
}

bool projectGenerator::outputProjectExports(const string& sProjectName, const StaticList& vIncludeDirs)
{
    string sExportList;
    if (!findFile(this->m_sProjectDir + "\\*.v", sExportList)) {
        cout << "  Error: Failed finding project exports (" << sProjectName << ")" << endl;
        return false;
    }

    //Open the input export file
    string sExportsFile;
    loadFromFile(this->m_sProjectDir + sExportList, sExportsFile);

    //Search for start of global tag
    string sGlobal = "global:";
    StaticList vExportStrings;
    uint uiFindPos = sExportsFile.find(sGlobal);
    if (uiFindPos != string::npos) {
        //Remove everything outside the global section
        uiFindPos += sGlobal.length();
        uint uiFindPos2 = sExportsFile.find("local:", uiFindPos);
        sExportsFile = sExportsFile.substr(uiFindPos, uiFindPos2 - uiFindPos);

        //Remove any comments
        uiFindPos = sExportsFile.find('#');
        while (uiFindPos != string::npos) {
            //find end of line
            uiFindPos2 = sExportsFile.find(10, uiFindPos + 1); //10 is line feed
            sExportsFile.erase(uiFindPos, uiFindPos2 - uiFindPos + 1);
            uiFindPos = sExportsFile.find('#', uiFindPos + 1);
        }

        //Clean any remaining white space out
        sExportsFile.erase(remove_if(sExportsFile.begin(), sExportsFile.end(), ::isspace), sExportsFile.end());

        //Get any export strings
        uiFindPos = 0;
        uiFindPos2 = sExportsFile.find(';');
        while (uiFindPos2 != string::npos) {
            vExportStrings.push_back(sExportsFile.substr(uiFindPos, uiFindPos2 - uiFindPos));
            uiFindPos = uiFindPos2 + 1;
            uiFindPos2 = sExportsFile.find(';', uiFindPos);
        }
    } else {
        cout << "  Error: Failed finding global start in project exports (" << sExportList << ")" << endl;
        return false;
    }

    //Create a test file to read in definitions
    string sOutDir = "../../../../../msvc/";
    string sCLExtra = "/I\"" + sOutDir + "include/\"";
    for (StaticList::const_iterator vitIt = vIncludeDirs.cbegin(); vitIt < vIncludeDirs.cend(); vitIt++) {
        string sIncludeDir = *vitIt;
        uint uiFindPos2 = sIncludeDir.find("$(OutDir)");
        if (uiFindPos2 != string::npos) {
            sIncludeDir.replace(uiFindPos2, 9, sOutDir);
        }
        replace(sIncludeDir.begin(), sIncludeDir.end(), '\\', '/');
        uiFindPos2 = sIncludeDir.find("$(");
        if (uiFindPos2 != string::npos) {
            sIncludeDir.replace(uiFindPos2, 2, "%");
        }
        uiFindPos2 = sIncludeDir.find(")");
        if (uiFindPos2 != string::npos) {
            sIncludeDir.replace(uiFindPos2, 1, "%");
        }
        sCLExtra += " /I\"" + sIncludeDir + '\"';
    }

    //Split each source file into different directories to avoid name clashes
    map<string, StaticList> mDirectoryObjects;
    for (StaticList::iterator itI = m_vCIncludes.begin(); itI < m_vCIncludes.end(); itI++) {
        //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
        uint uiPos = itI->rfind("..\\");
        uiPos = (uiPos == string::npos) ? 0 : uiPos + 3;
        uint uiPos2 = itI->rfind('\\');
        uiPos2 = (uiPos2 == string::npos) ? string::npos : uiPos2 - uiPos;
        string sFolderName = itI->substr(uiPos, uiPos2);
        mDirectoryObjects[sFolderName].push_back(*itI);
    }
    for (StaticList::iterator itI = m_vCPPIncludes.begin(); itI < m_vCPPIncludes.end(); itI++) {
        //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
        uint uiPos = itI->rfind("..\\");
        uiPos = (uiPos == string::npos) ? 0 : uiPos + 3;
        uint uiPos2 = itI->rfind('\\');
        uiPos2 = (uiPos2 == string::npos) ? string::npos : uiPos2 - uiPos;
        string sFolderName = itI->substr(uiPos, uiPos2);
        mDirectoryObjects[sFolderName].push_back(*itI);
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
    string sProjectNameShort = sProjectName.substr(3); //The full name minus the lib prefix
    sCLLaunchBat += "mkdir " + sProjectNameShort + " > nul 2>&1\n";
    for (map<string, StaticList>::iterator itI = mDirectoryObjects.begin(); itI != mDirectoryObjects.end(); itI++) {
        //Need to make output directory so cl doesnt fail outputting objs
        string sDirName = sProjectNameShort + "\\" + itI->first;
        sCLLaunchBat += "mkdir " + sDirName + " > nul 2>&1\n";
        const uint uiRowSize = 32;
        uint uiNumCLCalls = (uint)ceilf((float)itI->second.size() / (float)uiRowSize);
        uint uiTotalPos = 0;

        //Split calls into groups of 50 to prevent batch file length limit
        for (uint uiI = 0; uiI < uiNumCLCalls; uiI++) {
            sCLLaunchBat += "cl.exe";
            sCLLaunchBat += " /I\"../../\" /I\"../../../\" " + sCLExtra + " /Fo\"" + sDirName + "/\" /D\"_DEBUG\" /D\"WIN32\" /D\"_WINDOWS\" /D\"HAVE_AV_CONFIG_H\" /D\"inline=__inline\" /FI\"compat.h\" /FR\"" + sDirName + "/\" /c /MP /w /nologo";
            uint uiStartPos = uiTotalPos;
            for (uiTotalPos; uiTotalPos < min(uiStartPos + uiRowSize, itI->second.size()); uiTotalPos++) {
                sCLLaunchBat += " \"../../" + itI->second[uiTotalPos] + "\"";
            }
            sCLLaunchBat += " > log.txt\nif %errorlevel% neq 0 goto exitFail\n";
        }
    }
    sCLLaunchBat += "del /F /S /Q *.obj > nul 2>&1\ndel log.txt > nul 2>&1\n";
    sCLLaunchBat += "exit /b 0\n:exitFail\nrmdir /S /Q " + sProjectNameShort + "\nexit /b 1";
    if (!writeToFile("test.bat", sCLLaunchBat)) {
        return false;
    }

    if (0 != system("test.bat")) {
        cout << "  Error: Failed calling temp.bat. Ensure you have Visual Studio or the Microsoft compiler installed and that any required dependencies are available.\nSee log.txt for further details." << endl;
        //Remove the test header files
        deleteFile("test.bat");
        deleteFolder(sProjectNameShort);
        return false;
    }

    //Remove the compilation objects
    deleteFile("test.bat");

    //Loaded in the compiler passed files
    StaticList vSBRFiles;
    StaticList vModuleExports;
    StaticList vModuleDataExports;
    findFiles(sProjectNameShort + "\\*.sbr", vSBRFiles);
    for (StaticList::iterator itSBR = vSBRFiles.begin(); itSBR < vSBRFiles.end(); itSBR++) {
        string sSBRFile;
        loadFromFile(*itSBR, sSBRFile, true);

        //Search through file for module exports
        for (StaticList::iterator itI = vExportStrings.begin(); itI < vExportStrings.end(); itI++) {
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
            uiFindPos = itI->find('*');
            if (uiFindPos != string::npos) {
                //Strip the wild card (Note: assumes wild card is at the end!)
                string sSearch = itI->substr(0, uiFindPos);

                //Search for all occurrences
                uiFindPos = sSBRFile.find(sSearch);
                while (uiFindPos != string::npos) {
                    //Find end of name signaled by NULL character
                    uint uiFindPos2 = sSBRFile.find((char)0x00, uiFindPos + 1);

                    //Check if this is a define
                    uint uiFindPos3 = sSBRFile.rfind((char)0x00, uiFindPos - 3);
                    while (sSBRFile.at(uiFindPos3 - 1) == (char)0x00) {
                        //Skip if there was a NULL in ID
                        --uiFindPos3;
                    }
                    uint uiFindPosDiff = uiFindPos - uiFindPos3;
                    if ((sSBRFile.at(uiFindPos3 - 1) == '@') &&
                        (((uiFindPosDiff == 3) && (sSBRFile.at(uiFindPos3 - 3) == (char)0x03)) ||
                         ((uiFindPosDiff == 4) && (sSBRFile.at(uiFindPos3 - 3) == 'C')))) {
                        //Check if this is a data or function name
                        string sFoundName = sSBRFile.substr(uiFindPos, uiFindPos2 - uiFindPos);
                        if ((sSBRFile.at(uiFindPos3 - 2) == (char)0x01)) {
                            //This is a function
                            if (find(vModuleExports.begin(), vModuleExports.end(), sFoundName) == vModuleExports.end()) {
                                vModuleExports.push_back(sFoundName);
                            }
                        } else if ((sSBRFile.at(uiFindPos3 - 2) == (char)0x04)) {
                            //This is data
                            if (find(vModuleDataExports.begin(), vModuleDataExports.end(), sFoundName) == vModuleDataExports.end()) {
                                vModuleDataExports.push_back(sFoundName);
                            }
                        }
                    }

                    //Get next
                    uiFindPos = sSBRFile.find(sSearch, uiFindPos2 + 1);
                }
            } else {
                uiFindPos = sSBRFile.find(*itI);
                //Make sure the match is an exact one
                uint uiFindPos3;
                while ((uiFindPos != string::npos)) {
                    if (sSBRFile.at(uiFindPos + itI->length()) == (char)0x00) {
                        uiFindPos3 = sSBRFile.rfind((char)0x00, uiFindPos - 3);
                        while (sSBRFile.at(uiFindPos3 - 1) == (char)0x00) {
                            //Skip if there was a NULL in ID
                            --uiFindPos3;
                        }
                        uint uiFindPosDiff = uiFindPos - uiFindPos3;
                        if ((sSBRFile.at(uiFindPos3 - 1) == '@') &&
                            (((uiFindPosDiff == 3) && (sSBRFile.at(uiFindPos3 - 3) == (char)0x03)) ||
                             ((uiFindPosDiff == 4) && (sSBRFile.at(uiFindPos3 - 3) == 'C')))) {
                            break;
                        }
                    }
                    uiFindPos = sSBRFile.find(*itI, uiFindPos + 1);
                }
                if (uiFindPos == string::npos) {
                    continue;
                }
                //Check if this is a data or function name
                if ((sSBRFile.at(uiFindPos3 - 2) == (char)0x01)) {
                    //This is a function
                    if (find(vModuleExports.begin(), vModuleExports.end(), *itI) == vModuleExports.end()) {
                        vModuleExports.push_back(*itI);
                    }
                } else if ((sSBRFile.at(uiFindPos3 - 2) == (char)0x04)) {
                    //This is data
                    if (find(vModuleDataExports.begin(), vModuleDataExports.end(), *itI) == vModuleDataExports.end()) {
                        vModuleDataExports.push_back(*itI);
                    }
                }
            }
        }
    }
    //Remove the test sbr files
    deleteFolder(sProjectNameShort);

    //Check for any exported functions in asm files
    for (StaticList::iterator itASM = m_vYASMIncludes.begin(); itASM < m_vYASMIncludes.end(); itASM++) {
        string sASMFile;
        loadFromFile("../../" + *itASM, sASMFile);

        //Search through file for module exports
        for (StaticList::iterator itI = vExportStrings.begin(); itI < vExportStrings.end(); itI++) {
            //Check if it is a wild card search
            uiFindPos = itI->find('*');
            const string sInvalidChars = ",.(){}[]`'\"+-*/!@#$%^&*<>|;\\= \n\t\0";
            if (uiFindPos != string::npos) {
                //Strip the wild card (Note: assumes wild card is at the end!)
                string sSearch = ' ' + itI->substr(0, uiFindPos);
                //Search for all occurrences
                uiFindPos = sASMFile.find(sSearch);
                while ((uiFindPos != string::npos) && (uiFindPos > 0)) {
                    //Find end of name signaled by first non valid character
                    uint uiFindPos2 = sASMFile.find_first_of(sInvalidChars, uiFindPos + 1);
                    //Check this is valid function definition
                    if ((sASMFile.at(uiFindPos2) == '(') && (sInvalidChars.find(sASMFile.at(uiFindPos - 1)) == string::npos)) {
                        string sFoundName = sASMFile.substr(uiFindPos, uiFindPos2 - uiFindPos);
                        if (find(vModuleExports.begin(), vModuleExports.end(), sFoundName) == vModuleExports.end()) {
                            vModuleExports.push_back(sFoundName.substr(1));
                        }
                    }

                    //Get next
                    uiFindPos = sASMFile.find(sSearch, uiFindPos2 + 1);
                }
            } else {
                string sSearch = ' ' + *itI + '(';
                uiFindPos = sASMFile.find(*itI);
                //Make sure the match is an exact one
                if ((uiFindPos != string::npos) && (uiFindPos > 0) && (sInvalidChars.find(sASMFile.at(uiFindPos - 1)) == string::npos)) {
                    //Check this is valid function definition
                    if (find(vModuleExports.begin(), vModuleExports.end(), *itI) == vModuleExports.end()) {
                        vModuleExports.push_back(*itI);
                    }
                }
            }
        }
    }

    //Sort the exports
    sort(vModuleExports.begin(), vModuleExports.end());
    sort(vModuleDataExports.begin(), vModuleDataExports.end());

    //Create the export module string
    string sModuleFile = "EXPORTS\n";
    for (StaticList::iterator itI = vModuleExports.begin(); itI < vModuleExports.end(); itI++) {
        sModuleFile += "    " + *itI + "\n";
    }
    for (StaticList::iterator itI = vModuleDataExports.begin(); itI < vModuleDataExports.end(); itI++) {
        sModuleFile += "    " + *itI + " DATA\n";
    }

    string sDestinationFile = "../../" + sProjectName + ".def";
    if (!writeToFile(sDestinationFile, sModuleFile)) {
        return false;
    }
    return true;
}

void projectGenerator::outputBuildEvents(const string& sProjectName, string & sProjectTemplate)
{
    //After </Lib> and </Link> and the post and then pre build events
    const string asLibLink[2] = {"</Lib>", "</Link>"};
    const string sPostbuild = "\n    <PostBuildEvent>\n\
      <Command>";
    const string sPostbuildClose = "</Command>\n\
    </PostBuildEvent>";
    const string sInclude = "mkdir $(OutDir)\\include\n\
mkdir $(OutDir)\\include\\";
    const string sCopy = "\ncopy ";
    const string sCopyEnd = " $(OutDir)\\include\\";
    const string sLicense = "\nmkdir $(OutDir)\\licenses";
    string sLicenseName = m_ConfigHelper.m_sProjectName;
    transform(sLicenseName.begin(), sLicenseName.end(), sLicenseName.begin(), ::tolower);
    const string sLicenseEnd = " $(OutDir)\\licenses\\" + sLicenseName + ".txt";
    const string sPrebuild = "\n    <PreBuildEvent>\n\
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
)";
    const string sPrebuildDir = "\nif exist $(OutDir)\\include\\" + sProjectName + " (\n\
rd /s /q $(OutDir)\\include\\" + sProjectName + "\n\
cd ../\n\
cd $(ProjectDir)\n\
)";
    const string sPrebuildClose = "</Command>\n    </PreBuildEvent>";
    //Get the correct license file
    string sLicenseFile;
    if (m_ConfigHelper.getConfigOption("nonfree")->m_sValue.compare("1") == 0) {
        sLicenseFile = "..\\COPYING.GPLv3"; //Technically this has no license as it is unredistributable but we get the closest thing for now
    } else if (m_ConfigHelper.getConfigOption("gplv3")->m_sValue.compare("1") == 0) {
        sLicenseFile = "..\\COPYING.GPLv3";
    } else if (m_ConfigHelper.getConfigOption("lgplv3")->m_sValue.compare("1") == 0) {
        sLicenseFile = "..\\COPYING.LGPLv3";
    } else if (m_ConfigHelper.getConfigOption("gpl")->m_sValue.compare("1") == 0) {
        sLicenseFile = "..\\COPYING.GPLv2";
    } else {
        sLicenseFile = "..\\COPYING.LGPLv2.1";
    }
    //Generate the pre build and post build string
    string sAdditional;
    //Add the post build event
    sAdditional += sPostbuild;
    if (m_vHIncludes.size() > 0) {
        sAdditional += sInclude;
        sAdditional += sProjectName;
        for (StaticList::iterator vitHeaders = m_vHIncludes.begin(); vitHeaders<m_vHIncludes.end(); vitHeaders++) {
            sAdditional += sCopy;
            replace(vitHeaders->begin(), vitHeaders->end(), '/', '\\');
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
    if (m_vHIncludes.size() > 0) {
        sAdditional += sPrebuildDir;
    }
    sAdditional += sPrebuildClose;

    for (uint uiI = 0; uiI < 2; uiI++) {
        uint uiFindPos = sProjectTemplate.find(asLibLink[uiI]);
        while (uiFindPos != string::npos) {
            uiFindPos += asLibLink[uiI].length();
            //Add to output
            sProjectTemplate.insert(uiFindPos, sAdditional);
            uiFindPos += sAdditional.length();
            //Get next
            uiFindPos = sProjectTemplate.find(asLibLink[uiI], uiFindPos + 1);
        }
    }
}

void projectGenerator::outputIncludeDirs(const StaticList & vIncludeDirs, string & sProjectTemplate)
{
    if (vIncludeDirs.size() > 0) {
        string sAddInclude;
        for (StaticList::const_iterator vitIt = vIncludeDirs.cbegin(); vitIt < vIncludeDirs.cend(); vitIt++) {
            sAddInclude += *vitIt + ";";
        }
        const string sAddIncludeDir = "<AdditionalIncludeDirectories>";
        uint uiFindPos = sProjectTemplate.find(sAddIncludeDir);
        while (uiFindPos != string::npos) {
            //Add to output
            uiFindPos += sAddIncludeDir.length(); //Must be added first so that it is before $(IncludePath) as otherwise there are errors
            sProjectTemplate.insert(uiFindPos, sAddInclude);
            uiFindPos += sAddInclude.length();
            //Get next
            uiFindPos = sProjectTemplate.find(sAddIncludeDir, uiFindPos + 1);
        }
    }
}

void projectGenerator::outputLibDirs(const StaticList & vLib32Dirs, const StaticList & vLib64Dirs, string & sProjectTemplate)
{
    if ((vLib32Dirs.size() > 0) || (vLib64Dirs.size() > 0)) {
        //Add additional lib includes to include list based on current config
        string sAddLibs[2];
        for (StaticList::const_iterator vitIt = vLib32Dirs.cbegin(); vitIt < vLib32Dirs.cend(); vitIt++) {
            sAddLibs[0] += *vitIt + ";";
        }
        for (StaticList::const_iterator vitIt = vLib64Dirs.cbegin(); vitIt < vLib64Dirs.cend(); vitIt++) {
            sAddLibs[1] += *vitIt + ";";
        }
        const string sAddLibDir = "<AdditionalLibraryDirectories>";
        uint ui32Or64 = 0; //start with 32 (assumes projects are ordered 32 then 64 recursive)
        uint uiFindPos = sProjectTemplate.find(sAddLibDir);
        while (uiFindPos != string::npos) {
            //Add to output
            uiFindPos += sAddLibDir.length();
            sProjectTemplate.insert(uiFindPos, sAddLibs[ui32Or64]);
            uiFindPos += sAddLibs[ui32Or64].length();
            //Get next
            uiFindPos = sProjectTemplate.find(sAddLibDir, uiFindPos + 1);
            ui32Or64 = !ui32Or64;
        }
    }
}

void projectGenerator::outputYASMTools(string & sProjectTemplate)
{
    const string sYASMDefines = "\n\
    <YASM>\n\
      <IncludePaths>..\\;.\\;..\\libavcodec;%(IncludePaths)</IncludePaths>\n\
      <PreIncludeFile>config.asm</PreIncludeFile>\n\
      <Debug>true</Debug>\n\
    </YASM>";
    const string sYASMProps = "\n\
  <ImportGroup Label=\"ExtensionSettings\">\n\
    <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\vsyasm.props\" />\n\
  </ImportGroup>";
    const string sYASMTargets = "\n\
  <ImportGroup Label=\"ExtensionTargets\">\n\
    <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\vsyasm.targets\" />\n\
  </ImportGroup>";
    const string sFindProps = "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />";
    const string sFindTargets = "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />";

    if ((m_ConfigHelper.getConfigOptionPrefixed("HAVE_YASM")->m_sValue.compare("1") == 0) && (m_vYASMIncludes.size() > 0)) {
        //Add YASM defines
        const string sEndPreBuild = "</PreBuildEvent>";
        uint uiFindPos = sProjectTemplate.find(sEndPreBuild);
        while (uiFindPos != string::npos) {
            uiFindPos += sEndPreBuild.length();
            //Add to output
            sProjectTemplate.insert(uiFindPos, sYASMDefines);
            uiFindPos += sYASMDefines.length();
            //Get next
            uiFindPos = sProjectTemplate.find(sEndPreBuild, uiFindPos + 1);
        }

        //Add YASM build customization
        uiFindPos = sProjectTemplate.find(sFindProps) + sFindProps.length();
        //After <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" /> add yasm props
        sProjectTemplate.insert(uiFindPos, sYASMProps);
        uiFindPos += sYASMProps.length();
        uiFindPos = sProjectTemplate.find(sFindTargets) + sFindTargets.length();
        //After <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" /> add yasm target
        sProjectTemplate.insert(uiFindPos, sYASMTargets);
        uiFindPos += sYASMTargets.length();
    }
}

bool projectGenerator::outputDependencyLibs(const string & sProjectName, string & sProjectTemplate, bool bProgram)
{
    //Check current libs list for valid lib names
    for (StaticList::iterator vitLib = m_vLibs.begin(); vitLib < m_vLibs.end(); vitLib++) {
        //prepend lib if needed
        if (vitLib->find("lib") != 0) {
            *vitLib = "lib" + *vitLib;
        }
    }

    //Add additional dependencies based on current config to Libs list
    buildInterDependencies(sProjectName, m_vLibs);
    m_mProjectLibs[sProjectName] = m_vLibs; //Backup up current libs for solution
    StaticList vAddLibs;
    buildDependencies(sProjectName, m_vLibs, vAddLibs);

    if ((m_vLibs.size() > 0) || (vAddLibs.size() > 0)) {
        //Create list of additional ffmpeg dependencies
        string sAddFFmpegLibs[4]; //debug, release, debugDll, releaseDll
        for (StaticList::iterator vitLib = m_mProjectLibs[sProjectName].begin(); vitLib < m_mProjectLibs[sProjectName].end(); vitLib++) {
            sAddFFmpegLibs[0] += *vitLib;
            sAddFFmpegLibs[0] += "d.lib;";
            sAddFFmpegLibs[1] += *vitLib;
            sAddFFmpegLibs[1] += ".lib;";
            sAddFFmpegLibs[2] += vitLib->substr(3);
            sAddFFmpegLibs[2] += "d.lib;";
            sAddFFmpegLibs[3] += vitLib->substr(3);
            sAddFFmpegLibs[3] += ".lib;";
        }
        //Create List of additional dependencies
        string sAddDeps[4]; //debug, release, debugDll, releaseDll
        for (StaticList::iterator vitLib = m_vLibs.begin() + m_mProjectLibs[sProjectName].size(); vitLib < m_vLibs.end(); vitLib++) {
            sAddDeps[0] += *vitLib;
            sAddDeps[0] += "d.lib;";
            sAddDeps[1] += *vitLib;
            sAddDeps[1] += ".lib;";
            sAddDeps[2] += vitLib->substr(3);
            sAddDeps[2] += "d.lib;";
            sAddDeps[3] += vitLib->substr(3);
            sAddDeps[3] += ".lib;";
        }
        //Create List of additional external dependencies
        string sAddExternDeps;
        for (StaticList::iterator vitLib = vAddLibs.begin(); vitLib < vAddLibs.end(); vitLib++) {
            sAddExternDeps += *vitLib;
            sAddExternDeps += ".lib;";
        }
        //Add to Additional Dependencies
        string asLibLink2[2] = {"<Link>", "<Lib>"};
        for (uint uiLinkLib = 0; uiLinkLib < ((bProgram) ? 1 : 2); uiLinkLib++) {
            //loop over each debug/release sequence
            uint uiFindPos = sProjectTemplate.find(asLibLink2[uiLinkLib]);
            for (uint uiDebugRelease = 0; uiDebugRelease < 2; uiDebugRelease++) {
                uint uiMax = ((uiDebugRelease == 0) && (uiLinkLib == 1)) ? 2 : 4; //No LTO option in debug
                //x86, x64, x86LTO/Static, x64LTO/Static -- x86, x64, x86DLL, x64DLL (projects)
                for (uint uiConf = 0; uiConf < uiMax; uiConf++) {
                    uiFindPos = sProjectTemplate.find("%(AdditionalDependencies)", uiFindPos);
                    if (uiFindPos == string::npos) {
                        cout << "  Error: Failed finding dependencies in template." << endl;
                        return false;
                    }
                    uint uiAddIndex = uiDebugRelease;
                    if ((uiLinkLib == 0) && (((!bProgram) && (uiConf < 2)) || ((bProgram) && (uiConf >= 2)))) {
                        //Use DLL libs
                        uiAddIndex += 2;
                    }
                    string sAddString;
                    if (uiLinkLib == 0) {
                        //If the dependency is actually for one of the ffmpeg libs then we can ignore it in static linking mode
                        //  as this just causes unnecessary code bloat
                        if ((!bProgram) || (uiConf >= 2)) {
                            sAddString = sAddFFmpegLibs[uiDebugRelease + 2]; //Always link ffmpeg libs to the dll even in DLLStatic
                        }
                        else if (bProgram) {
                            sAddString = sAddFFmpegLibs[uiDebugRelease];
                        }
                    }
                    //Add to output
                    sAddString += sAddDeps[uiAddIndex] + sAddExternDeps;
                    sProjectTemplate.insert(uiFindPos, sAddString);
                    uiFindPos += sAddString.length();
                    //Get next
                    uiFindPos = sProjectTemplate.find(asLibLink2[uiLinkLib], uiFindPos + 1);
                }
            }
        }
    }
    return true;
}

