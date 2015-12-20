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

#ifndef _PROJECTGENERATOR_H_
#define _PROJECTGENERATOR_H_

#include "configGenerator.h"

#include <fstream>
#include <set>

class projectGenerator
{
private:
    typedef vector<string> StaticList;
    typedef map<string,StaticList> UnknownList;
    ifstream        m_ifInputFile;
    string          m_sInLine;
    StaticList      m_vIncludes;
    StaticList      m_vCPPIncludes;
    StaticList      m_vCIncludes;
    StaticList      m_vYASMIncludes;
    StaticList      m_vHIncludes;
    UnknownList     m_mReplaceIncludes;
    StaticList      m_vLibs;
    UnknownList     m_mUnknowns;
    string          m_sProjectDir;

    map<string,StaticList> m_mProjectLibs;

    configGenerator::DefaultValuesList m_ReplaceValues;

public:

    configGenerator m_ConfigHelper;

    bool passAllMake( );

    void deleteCreatedFiles();

private:

    bool outputProject( );

    bool outputProgramProject(const string& sProjectName, const string& sDestinationFile, const string& sDestinationFilterFile);

    bool outputSolution();

    bool passStaticIncludeObject( uint & uiStartPos, uint & uiEndPos, StaticList & vStaticIncludes );

    bool passStaticIncludeLine( uint uiStartPos, StaticList & vStaticIncludes );

    bool passStaticInclude( uint uiILength, StaticList & vStaticIncludes );

    bool passDynamicIncludeObject( uint & uiStartPos, uint & uiEndPos, string & sIdent, StaticList & vIncludes );

    bool passDynamicIncludeLine( uint uiStartPos, string & sIdent, StaticList & vIncludes );

    bool passDynamicInclude( uint uiILength, StaticList & vIncludes );

    bool passCInclude( );

    bool passDCInclude( );

    bool passYASMInclude( );

    bool passDYASMInclude( );

    bool passMMXInclude( );

    bool passDMMXInclude( );

    bool passHInclude( uint uiCutPos = 7 );

    bool passDHInclude( );

    bool passLibInclude( );

    bool passDLibInclude( );

    bool passDUnknown( );

    bool passDLibUnknown( );

    bool passMake( );

    bool passProgramMake(const string& sProjectName);

    bool findSourceFile( const string & sFile, const string & sExtension, string & sRetFileName );

    bool findSourceFiles( const string & sFile, const string & sExtension, vector<string> & vRetFiles );

    void makeFileProjectRelative(const string & sFileName, string & sRetFileName);

    void makeFileGeneratorRelative(const string & sFileName, string & sRetFileName);

    void buildInterDependenciesHelper( const StaticList & vConfigOptions, const StaticList & vAddDeps, StaticList & vLibs );

    void buildInterDependencies( const string & sProjectName, StaticList & vLibs );

    void buildDependencies( const string & sProjectName, StaticList & vLibs, StaticList & vAddLibs );

    void buildDependencyDirs(const string & sProjectName, StaticList & vIncludeDirs, StaticList & vLib32Dirs, StaticList & vLib64Dirs);

    void buildProjectDependencies( const string & sProjectName, map<string,bool> & mProjectDeps );

    void buildProjectGUIDs( map<string, string> & mKeys );

    bool checkProjectFiles(const string& sProjectName);

    bool createReplaceFiles(const StaticList& vReplaceIncludes, StaticList& vExistingIncludes, const string& sProjectName);

    bool findProjectFiles(const StaticList& vIncludes, StaticList& vCIncludes, StaticList& vCPPIncludes, StaticList& vASMIncludes, StaticList& vHIncludes);

    void outputTemplateTags(const string& sProjectName, string& sProjectTemplate, string& sFilterTemplate);

    void outputSourceFileType(StaticList& vFileList, const string& sType, const string& sFilterType, string & sProjectTemplate, string & sFilterTemplate, StaticList& vFoundObjects, set<string>& vFoundFilters, bool bCheckExisting);

    void outputSourceFiles(const string& sProjectName, string& sProjectTemplate, string& sFilterTemplate);

    bool outputProjectExports(const string& sProjectName, const StaticList& vIncludeDirs);

    void outputBuildEvents(const string& sProjectName, string & sProjectTemplate);

    void outputIncludeDirs(const StaticList& vIncludeDirs, string & sProjectTemplate);

    void outputLibDirs(const StaticList& vLib32Dirs, const StaticList& vLib64Dirs, string & sProjectTemplate);

    void outputYASMTools(string & sProjectTemplate);

    bool outputDependencyLibs(const string& sProjectName, string & sProjectTemplate, bool bProgram=false);
};

#endif
