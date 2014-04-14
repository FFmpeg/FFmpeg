
#include "projectGenerator.h"

#include <algorithm>
#include <set>
#include <Windows.h>

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
            m_sProjectDir = "../../../lib" + *vitLib + "/";
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
            m_sProjectDir += "x86/";
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
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ../../ so the file is up 2 directories
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
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ../../ so the file is up 2 directories
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
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ../../ so the file is up 2 directories
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
        *itIt = sRetFileName.substr( 6 ); //Remove the preceding ../../ so the file is up 2 directories
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
            m_vCIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ../../ so the file is up 2 directories
        }
        else if( findSourceFile( *itIt, ".cpp", sRetFileName ) )
        {
            //Found a C++ File to include
            if( find( m_vCPPIncludes.begin(), m_vCPPIncludes.end(), sRetFileName ) != m_vCPPIncludes.end() )
            {
                //skip this item
                continue;
            }
            m_vCPPIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ../../ so the file is up 2 directories
        }
        else if( findSourceFile( *itIt, ".asm", sRetFileName ) )
        {
            //Found a C++ File to include
            if( find( m_vYASMIncludes.begin(), m_vYASMIncludes.end(), sRetFileName ) != m_vYASMIncludes.end() )
            {
                //skip this item
                continue;
            }
            m_vYASMIncludes.push_back( sRetFileName.substr( 6 ) ); //Remove the preceding ../../ so the file is up 2 directories
        }
        else
        {
            cout << "  Error: Could not find valid source file for object (" << *itIt << ")" << endl;
            return false;
        }
    }

    //We now have complete list of all the files the we need
    uint uiSPos = m_sProjectDir.rfind( '/', m_sProjectDir.length()-2 )+1;
    string sProjectName = m_sProjectDir.substr( uiSPos, m_sProjectDir.length()-1-uiSPos );
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
    if( m_ConfigHelper.m_sToolchain.compare("msvc") == 0 )
    {
        sToolchain = "v120";
    }
    else
    {
        sToolchain = "Intel C++ Compiler XE 14.0";
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
            string sFileName = *vitInclude;
            replace( sFileName.begin(), sFileName.end(), '/', '\\' );
            sCFiles += sFileName;
            sCFilesFilt += sFileName;
            sCFiles += sCLCompileClose;
            sCFilesFilt += sCLCompileClose;
            //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
            uint uiPos = sFileName.rfind( "..\\" );
            uiPos = (uiPos == string::npos)? 0 : uiPos+3;
            string sObjectName = sFileName.substr( uiPos );
            replace( sObjectName.begin(), sObjectName.end(), '\\', '_' );
            //Replace the extension with obj
            uint uiPos2 = sObjectName.rfind( '.' );
            sObjectName.resize( uiPos2 );
            sCFiles += sCLCompileObject;
            sCFiles += sObjectName;
            sCFiles += sCLCompileObjectClose;
            //Add the filters Filter
            sCFilesFilt += sFilterSource;
            string sFolderName = sFileName.substr( uiPos, sFileName.rfind( '\\' )-uiPos );
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
            string sFileName = *vitInclude;
            replace( sFileName.begin(), sFileName.end(), '/', '\\' );
            sCPPFiles += sFileName;
            sCPPFilesFilt += sFileName;
            sCPPFiles += sCLCompileClose;
            sCPPFilesFilt += sCLCompileClose;
            //Several input source files have the same name so we need to explicitly specify an output object file otherwise they will clash
            uint uiPos = sFileName.rfind( "..\\" );
            uiPos = (uiPos == string::npos)? 0 : uiPos+3;
            string sObjectName = sFileName.substr( uiPos );
            replace( sObjectName.begin(), sObjectName.end(), '\\', '_');
            //Replace the extension with obj
            uint uiPos2 = sObjectName.rfind( '.' );
            sObjectName.resize( uiPos );
            sCPPFiles += sCLCompileObject;
            sCPPFiles += sObjectName;
            sCPPFiles += sCLCompileObjectClose;
            //Add the filters Filter
            sCPPFilesFilt += sFilterSource;
            string sFolderName = sFileName.substr( uiPos, sFileName.rfind( '\\' )-uiPos );
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
            string sFileName = *vitInclude;
            replace( sFileName.begin(), sFileName.end(), '/', '\\' );
            sHFiles += sFileName;
            sHFilesFilt += sFileName;
            //Close the current item
            sHFiles += sCLIncludeEnd;
            sHFilesFilt += sCLIncludeEndFilt;
            //Add the filters Filter
            sHFilesFilt += sFilterHeader;
            uint uiPos = sFileName.rfind( "..\\" );
            uiPos = (uiPos == string::npos)? 0 : uiPos+3;
            string sFolderName = sFileName.substr( uiPos, sFileName.rfind( '\\' )-uiPos );
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

    //Output ASM files in specific item group
    if( m_vYASMIncludes.size() > 0 )
    {
        if( m_ConfigHelper.getConfigOptionPrefixed("HAVE_YASM")->m_sValue.compare("1") == 0 )
        {
            string sYASMInclude = "\n    <YASM Include=\"";
            string sYASMIncludeEnd = "\" />";
            string sYASMIncludeEndFilt = "\">";
            string sYASMIncludeClose = "\n    </YASM>";
            string sYASMFiles = sItemGroup;
            string sYASMFilesFilt = sItemGroup;
            for( StaticList::iterator vitInclude=m_vYASMIncludes.begin(); vitInclude<m_vYASMIncludes.end(); vitInclude++ )
            {
                //Output YASM objects
                sYASMFiles += sYASMInclude;
                sYASMFilesFilt += sYASMInclude;
                //Add the fileName
                string sFileName = *vitInclude;
                replace( sFileName.begin(), sFileName.end(), '/', '\\' );
                sYASMFiles += sFileName;
                sYASMFilesFilt += sFileName;
                //Close the current item
                sYASMFiles += sYASMIncludeEnd;
                sYASMFilesFilt += sYASMIncludeEndFilt;
                //Add the filters Filter
                sYASMFilesFilt += sFilterSource;
                uint uiPos = sFileName.rfind( "..\\" );
                uiPos = (uiPos == string::npos)? 0 : uiPos+3;
                string sFolderName = sFileName.substr( uiPos, sFileName.rfind( '\\' )-uiPos );
                if( sFolderName.length() > 0 )
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

    //After </Lib> and </Link> and the post and then pre build events
    string asLibLink[2] = { "</Lib>", "</Link>" };
    string sYASMDefines = "\n\
    <YASM>\n\
      <IncludePaths>..\\;.\\;..\\libavcodec;%(IncludePaths)</IncludePaths>\n\
      <PreIncludeFile>config.asm</PreIncludeFile>\n\
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
if exist ..\\avconfig.h (\n\
del ..\\avconfig.h\n\
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
    m_mProjectLibs[sProjectName] = m_vLibs; //Backup up current libs for solution
    StaticList vIncludes;
    StaticList vAddLibs;
    buildDependencies( sProjectName, m_vLibs, vAddLibs, vIncludes );
    vector<string> vLibraries;
    m_ConfigHelper.getConfigList( "LIBRARY_LIST", vLibraries );
    if( m_vLibs.size() > 0 )
    {
        //Add to Additional Dependencies
        string asLibLink2[2] = { "<Lib>", "<Link>" };
        string asExt[2] = { ".lib", ".dll.lib" };
        for( uint uiI=0; uiI<2; uiI++ )
        {
            //The additional dependency should be within the Link/Lib section
            string sAdditional;
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

                sAdditional += *vitLib;
                sAdditional += asExt[uiI];
                sAdditional += ";";
            }
            //Add each additional lib to the list
            for( StaticList::iterator vitLib=vAddLibs.begin(); vitLib<vAddLibs.end(); vitLib++ )
            {
                sAdditional += *vitLib;
                sAdditional += ".lib;";
            }
            uiFindPos = sProjectFile.find( asLibLink2[uiI] );
            while( uiFindPos != string::npos )
            {
                uiFindPos = sProjectFile.find( "%(AdditionalDependencies)", uiFindPos );
                //Add to output
                sProjectFile.insert( uiFindPos, sAdditional );
                uiFindPos += sAdditional.length( );
                //Get next
                uiFindPos = sProjectFile.find( asLibLink2[uiI], uiFindPos+1 );
            }
        }
    }
    //Add additional includes to include list based on current config
    string sAddInclude;
    for (StaticList::iterator vitIt=vIncludes.begin(); vitIt<vIncludes.end(); vitIt++ )
    {
        sAddInclude += *vitIt + ";";
    }
    string sAddIncludeDir = "<AdditionalIncludeDirectories>";
    uiFindPos = sProjectFile.find( sAddIncludeDir );
    while( uiFindPos != string::npos )
    {
        //Add to output
        uiFindPos += sAddIncludeDir.length(); //Must be added first so that it is before $(IncludePath) as otherwise there are errors
        sProjectFile.insert( uiFindPos, sAddInclude );
        uiFindPos += sAddInclude.length( );
        //Get next
        uiFindPos = sProjectFile.find( sAddIncludeDir, uiFindPos+1 );
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

    //Copy across the module definitions
    string sSourceFile = "../templates/" + sProjectName + ".def";
    //Open the input module def file
    m_ifInputFile.open( sSourceFile );
    if( !m_ifInputFile.is_open( ) )
    {
        cout << "  Error: Failed opening template module definition (" << sSourceFile << ")" << endl;
        return false;
    }

    //Load whole file into internal string
    string sModuleFile;
    m_ifInputFile.seekg( 0, m_ifInputFile.end );
    uiBufferSize = (uint)m_ifInputFile.tellg();
    m_ifInputFile.seekg( 0, m_ifInputFile.beg );
    sModuleFile.resize( uiBufferSize );
    m_ifInputFile.read( &sModuleFile[0], uiBufferSize );
    if( uiBufferSize != m_ifInputFile.gcount() )
    {
        sModuleFile.resize( (uint)m_ifInputFile.gcount() );
    }
    m_ifInputFile.close( );

    //Check the module file for configuration specific outputs
    string sModuleSearch = ";if ";
    string sModuleSearchEnd = ";endif";
    uiFindPos = sModuleFile.find( sModuleSearch );
    while( uiFindPos != string::npos )
    {
        //Get the config option
        uint uiFindPosE = uiFindPos+sModuleSearch.length();
        uint uiFindPos2 = sModuleFile.find( '\n', uiFindPosE );
        string sConfigOpt = sModuleFile.substr( uiFindPosE, uiFindPos2-uiFindPosE );
        configGenerator::ValuesList::iterator vitCO = m_ConfigHelper.getConfigOptionPrefixed( sConfigOpt );
        if( vitCO == m_ConfigHelper.m_vConfigValues.end() )
        {
            cout << "  Error: Unknown config option found in module file (" << sConfigOpt << ")" << endl;
            return false;
        }
        uint uiFindPosEnd = sModuleFile.find( sModuleSearchEnd, uiFindPos2 );;
        //Check if option is enabled
        if( vitCO->m_sValue.compare("1") == 0 )
        {
            //Remove the if and the endif
            sModuleFile.erase( uiFindPosEnd, sModuleSearchEnd.length()+1 ); //endif first as erasing the if will change uiFindPosEnd position
            sModuleFile.erase( uiFindPos, uiFindPos2-uiFindPos+1 ); //+1 for newline
        }
        else
        {
            //Remove everything from start of if to endif
            uiFindPosEnd += sModuleSearchEnd.length();
            sModuleFile.erase( uiFindPos, uiFindPosEnd-uiFindPos+1 ); //+1 for newline
        }
        //get next
        uiFindPos = sModuleFile.find( sModuleSearch, uiFindPos+1 );
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

    map<string,string> mLibKeys;
    mLibKeys["libavcodec"] = "B4824EFF-C340-425D-A4A8-E2E02A71A7AE";
    mLibKeys["libavdevice"] = "6E165FA4-44EB-4330-8394-9F0D76D8E03E";
    mLibKeys["libavfilter"] = "BC2E1028-66CD-41A0-AF90-EEBD8CC52787";
    mLibKeys["libavformat"] = "30A96E9B-8061-4F19-BD71-FDE7EA8F7929";
    mLibKeys["libavresample"] = "0096CB8C-3B04-462B-BF4F-0A9970A57C91";
    mLibKeys["libavutil"] = "CE6C44DD-6E38-4293-8AB3-04EE28CCA972";
    mLibKeys["libswresample"] = "3CE4A9EF-98B6-4454-B76E-3AD9C03A2114";
    mLibKeys["libswscale"] = "6D8A6330-8EBE-49FD-9281-0A396F9F28F2";
    mLibKeys["libpostproc"] = "4D9C457D-9ADA-4A12-9D06-42D80124C5AB";

    map<string,string> mProgramKeys;
    if( !m_ConfigHelper.m_bLibav )
    {
        mProgramKeys["ffmpeg"] = "4081C77E-F1F7-49FA-9BD8-A4D267C83716";
        mProgramKeys["ffplay"] = "E2A6865D-BD68-45B4-8130-EFD620F2C7EB";
        mProgramKeys["ffprobe"] = "147A422A-FA63-4724-A5D9-08B1CAFDAB59";
    }
    else
    {
        mProgramKeys["avconv"] = "4081C77E-F1F7-49FA-9BD8-A4D267C83716";
        mProgramKeys["avplay"] = "E2A6865D-BD68-45B4-8130-EFD620F2C7EB";
        mProgramKeys["avprobe"] = "147A422A-FA63-4724-A5D9-08B1CAFDAB59";
    }

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
    string sFileStart = "# Visual Studio 2013";
    uint uiPos = sSolutionFile.find( sFileStart ) + sFileStart.length( );
    
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
    if( m_ConfigHelper.m_sToolchain.compare("msvc") == 0 )
    {
        sToolchain = "v120";
    }
    else
    {
        sToolchain = "Intel C++ Compiler XE 14.0";
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
            ofstream ofProgramFile( sDestinationFile );
            if( !ofProgramFile.is_open( ) )
            {
                cout << "  Error: failed opening output program file (" << sDestinationFile << ")" << endl;
                return false;
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
            buildProgramIncludes( mitPrograms->first, vCIncludes, vHIncludes, vLibs, vIncDirs );
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
            string sDeps[4]; //staticd, sharedd, static, shared
            map<string, StaticList>::iterator mitLibraries = m_mProjectLibs.begin( );
            while( mitLibraries != m_mProjectLibs.end( ) )
            {
                sDeps[0] += mitLibraries->first;
                sDeps[0] += "d.lib;";
                sDeps[1] += mitLibraries->first;
                sDeps[1] += "d.dll.lib;";
                sDeps[2] += mitLibraries->first;
                sDeps[2] += ".lib;";
                sDeps[3] += mitLibraries->first;
                sDeps[3] += ".dll.lib;";
                mitLibraries++;
            }
            for( vector<string>::iterator vitDeps = vLibs.begin( ); vitDeps < vLibs.end( ); vitDeps++ )
            {
                sDeps[0] += *vitDeps + ";";
                sDeps[1] += *vitDeps + ";";
                sDeps[2] += *vitDeps + ";";
                sDeps[3] += *vitDeps + ";";
            }
            const string sAddDeps = "%(AdditionalDependencies)";
            uiFindPos = sProgramFile.find( sAddDeps );
            for( uint uDepPos = 0; uDepPos < 4; uDepPos++ )
            {
                //Do each Win32/x64 project pair
                for( uint uI = 0; uI < 2; uI++ )
                {
                    if( uiFindPos == string::npos )
                    {
                        cout << "  Error: Failed finding dependencies in program filters template." << endl;
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
            vAddedKeys.push_back( mProgramKeys[mitPrograms->first] );
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
    string sDebugWin32Build = "}.Debug|Win32.Build.0 = Debug|Win32";
    string sDebugx64 = "}.Debug|x64.ActiveCfg = Debug|x64";
    string sDebugx64Build = "}.Debug|x64.Build.0 = Debug|x64";
    string aBuildConfigs[4] = { "Debug", "DebugDLL", "Release", "ReleaseDLL" };
    string aBuildArchs[2] = { "Win32", "x64" };
    string aBuildTypes[2] = { ".ActiveCfg = ", ".Build.0 = " };
    string sAddPlatform;
    for( vector<string>::iterator vitIt=vAddedKeys.begin(); vitIt<vAddedKeys.end(); vitIt++ )
    {
        for( uint uiI=0; uiI<4; uiI++ )
        {
            for( uint uiJ=0; uiJ<2; uiJ++ )
            {
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
    if( sTag.find( '%' ) != string::npos )
    {
        //Invalid include (This happens when the include is a variable etc.)
        uiStartPos = m_sInLine.find( "%=", uiStartPos )+2;
        uiEndPos = m_sInLine.find( '%', uiStartPos );
        sTag = m_sInLine.substr( uiStartPos, uiEndPos-uiStartPos );
        //% is usually used to define an entire directory to include
        vector<string> vFiles;
        if( !findFiles( m_sProjectDir + sTag + "*.c", vFiles ) )
        {
            cout << "  Warning: Invalid include found (" << sTag << ")" << endl;
            return false;
        }
        //Prepend the full library path
        for( vector<string>::iterator vitFile=vFiles.begin(); vitFile<vFiles.end(); vitFile++ )
        {
            *vitFile = sTag + vitFile->substr( 0, vitFile->rfind('.') ); //Remove the unnecessary extension
        }
        //Check for any valid subdirectories
        uint uiEnd = vFiles.size( );
        findFiles( m_sProjectDir + sTag + "msvcrt/*.c", vFiles );
        for( vector<string>::iterator vitFile=vFiles.begin()+uiEnd; vitFile<vFiles.end(); vitFile++ )
        {
            *vitFile = sTag + "msvcrt/" + vitFile->substr( 0, vitFile->rfind('.') );
        }
        //Loop through each item and add to list
        for( vector<string>::iterator vitFile=vFiles.begin(); vitFile<vFiles.end(); vitFile++ )
        {
            //Check if object already included in internal list
            if( find( m_vCIncludes.begin(), m_vCIncludes.end(), *vitFile ) == m_vCIncludes.end() )
            {
                m_vCIncludes.push_back( *vitFile );
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

bool projectGenerator::passDynamicIncludeObject( uint & uiStartPos, uint & uiEndPos, const string & sIdent, StaticList & vIncludes )
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
            if( vitOption->m_sValue.compare("1") == 0 )
            {
                vIncludes.push_back( sTag );
                //cout << "  Found Dynamic: '" << sTag << "', '" << sIdent << "'" << endl;
            }
        }
    }
    return true;
}

bool projectGenerator::passDynamicIncludeLine( uint uiStartPos, const string & sIdent, StaticList & vIncludes )
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

bool projectGenerator::findFiles( const string & sFileSearch, vector<string> & VRetFiles )
{
    WIN32_FIND_DATA SearchFile;
    HANDLE SearchHandle=FindFirstFile( sFileSearch.c_str( ), &SearchFile );
    if( SearchHandle != INVALID_HANDLE_VALUE )
    {
        //Update the return filename list
        VRetFiles.push_back( SearchFile.cFileName );
        while( FindNextFile( SearchHandle, &SearchFile ) != 0 );
        {
            VRetFiles.push_back( SearchFile.cFileName );
        }
        FindClose( SearchHandle );
        return true;
    }
    return false;
}

bool projectGenerator::findSourceFile( const string & sFile, const string & sExtension, string & sRetFileName )
{
    string sFileName;
    sRetFileName = m_sProjectDir + sFile + sExtension;
    if( !findFile( sRetFileName, sFileName ) )
    {
        return false;
    }
    return true;
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
