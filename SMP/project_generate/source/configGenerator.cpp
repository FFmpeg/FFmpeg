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
 
#include "configGenerator.h"

#include <algorithm>

configGenerator::configGenerator( ) : 
    m_sWhiteSpace( " \t\n\0" ), 
    m_sToolchain( "msvc" ),
    m_bLibav( false ), 
    m_sProjectName( "FFMPEG" )
{
}

bool configGenerator::passConfig( )
{
    //Generate a new config file by scanning existing build chain files
    cout << "  Generating config files..." << endl;
    //Open configure file
    string sConfigFile = "../../../configure";
    ifstream ifConfigureFile( sConfigFile );
    if( !ifConfigureFile.is_open( ) )
    {
        cout << "  Error: failed opening configure file (" << sConfigFile << ")" << endl;
        return false;
    }

    //Load whole file into internal string
    ifConfigureFile.seekg( 0, ifConfigureFile.end );
    uint uiBufferSize = (uint)ifConfigureFile.tellg();
    ifConfigureFile.seekg( 0, ifConfigureFile.beg );
    m_sConfigureFile.resize( uiBufferSize );
    ifConfigureFile.read( &m_sConfigureFile[0], uiBufferSize );
    if( uiBufferSize != ifConfigureFile.gcount() )
    {
        m_sConfigureFile.resize( (uint)ifConfigureFile.gcount() );
    }
    ifConfigureFile.close( );

    //Search for start of config.h file parameters
    uint uiStartPos = m_sConfigureFile.find( "#define FFMPEG_CONFIG_H" );
    if( uiStartPos == string::npos )
    {
        //Check if this is instead a libav configure
        uiStartPos = m_sConfigureFile.find( "#define LIBAV_CONFIG_H" );
        if( uiStartPos == string::npos )
        {
            cout << "  Error: failed finding config.h start parameters" << endl;
            return false;
        }
        m_bLibav = true;
        m_sProjectName = "LIBAV";
    }
    //Move to end of header guard (+1 for new line)
    uiStartPos += 24;

    //Build default value list
    DefaultValuesList mDefaultValues;
    buildFixedValues( mDefaultValues );

    //Get each defined option till EOF
    uiStartPos = m_sConfigureFile.find( "#define", uiStartPos );
    uint uiConfigEnd = m_sConfigureFile.find( "EOF", uiStartPos );
    if( uiConfigEnd == string::npos )
    {
        cout << "  Error: failed finding config.h parameters end" << endl;
        return false;
    }
    uint uiEndPos = uiConfigEnd;
    while( ( uiStartPos != string::npos ) && ( uiStartPos < uiConfigEnd ) )
    {
        //Skip white space
        uiStartPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiStartPos+7 );
        //Get first string
        uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );
        string sConfigName = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos );
        //Get second string
        uiStartPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEndPos+1 );
        uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );
        string sConfigValue = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos );
        //Check if the value is a variable
        uint uiStartPos2 = sConfigValue.find( '$' );
        if( uiStartPos2 != string::npos )
        {
            //Check if it is a function call
            if( sConfigValue.at( uiStartPos2+1 ) == '(' )
            {
                uiEndPos = m_sConfigureFile.find( ')', uiStartPos );
                sConfigValue = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos+1 );
            }
            //Remove any quotes from the tag if there are any
            uint uiEndPos2 = (sConfigValue.at( sConfigValue.length( )-1 ) == '"')? sConfigValue.length( )-1 : sConfigValue.length( );
            //Find and replace the value
            DefaultValuesList::iterator mitVal = mDefaultValues.find( sConfigValue.substr( uiStartPos2, uiEndPos2-uiStartPos2 ) );
            if( mitVal == mDefaultValues.end( ) )
            {
                cout << "  Error: Unknown configuration operation found (" << sConfigValue.substr( uiStartPos2, uiEndPos2-uiStartPos2 ) << ")" << endl;
                return false;
            }
            //Check if we need to add the quotes back
            if( sConfigValue.at(0) == '"' )
            {
                //Replace the value with the default option in quotations
                sConfigValue = '"' + mitVal->second + '"';
            }
            else
            {
                //Replace the value with the default option
                sConfigValue = mitVal->second;
            }
        }

        //Add to the list
        m_vFixedConfigValues.push_back( ConfigPair( sConfigName, "", sConfigValue ) );

        //Find next
        uiStartPos = m_sConfigureFile.find( "#define", uiEndPos+1 );
    }

    //Find the end of this section
    uiConfigEnd = m_sConfigureFile.find( "#endif", uiConfigEnd+1 );
    if( uiConfigEnd == string::npos )
    {
        cout << "  Error: failed finding config.h header end" << endl;
        return false;
    }

    //Get the additional config values
    uiStartPos = m_sConfigureFile.find( "print_config", uiEndPos+3 );
    while( ( uiStartPos != string::npos ) && ( uiStartPos < uiConfigEnd ) )
    {
        //Add these to the config list
        //Find prefix
        uiStartPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiStartPos+12 );
        uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );
        string sPrefix = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos );
        //Skip unneeded var
        uiStartPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEndPos+1 );
        uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );

        //Find option list
        uiStartPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEndPos+1 );
        uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );
        string sList = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos );
        //Strip the variable prefix from start
        sList.erase( 0, 1 );

        //Create option list
        if( !passConfigList( sPrefix, "", sList ) )
        {
            return false;
        }

        //Check if multiple lines
        uiEndPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEndPos+1 );
        while( m_sConfigureFile.at(uiEndPos) == '\\' )
        {
            //Skip newline
            ++uiEndPos;
            uiStartPos = m_sConfigureFile.find_first_not_of( " \t", uiEndPos+1 );
            //Check for blank line
            if( m_sConfigureFile.at(uiStartPos) == '\n' )
            {
                break;
            }
            uiEndPos = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStartPos+1 );
            string sList = m_sConfigureFile.substr( uiStartPos, uiEndPos-uiStartPos );
            //Strip the variable prefix from start
            sList.erase( 0, 1 );

            //Create option list
            if( !passConfigList( sPrefix, "", sList ) )
            {
                return false;
            }
            uiEndPos = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEndPos+1 );
        }

        //Get next
        uiStartPos = m_sConfigureFile.find( "print_config", uiStartPos+1 );
    }
    //Mark the end of the config list. Any elements added after this are considered temporary and should not be exported
    m_uiConfigValuesEnd = m_vConfigValues.size( ); //must be uint in case of realloc
    //Load with default values
    return buildDefaultValues( );
}

bool configGenerator::passConfigFile( const string & stConfigFile )
{
    //Just pass in the config file that is passed in

    //Also need to get the avconfig file
    //??????? as currently this will use options from config.h but the prefix will be wrong so finding them write values wont work

    //Mark the end of the config list. Any elements added after this are considered temporary and should not be exported
    m_uiConfigValuesEnd = m_vConfigValues.size( ); //must be uint in case of realloc

    cout << "  Error: Directly passing pre-built config files is not supported yet!" << endl;
    return false;
}

bool configGenerator::changeConfig( const string & stOption )
{
    if( stOption.compare("--disable-devices") == 0 )
    {
        //Disable INDEV_LIST
        vector<string> vList;
        if( !getConfigList( "INDEV_LIST", vList ) )
        {
            return false;
        }
        //Disable all in list
        vector<string>::iterator vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
        //Disable OUTDEV_LIST
        vList.resize(0);
        if( !getConfigList( "OUTDEV_LIST", vList ) )
        {
            return false;
        }
        //Disable all in list
        vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
    }
    else if( stOption.compare("--disable-programs") == 0 )
    {
        //Disable PROGRAM_LIST
        vector<string> vList;
        if( !getConfigList( "PROGRAM_LIST", vList ) )
        {
            return false;
        }
        //Disable all in list
        vector<string>::iterator vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
    }
    else if( stOption.compare("--disable-everything") == 0 )
    {
        //Disable ALL_COMPONENTS
        vector<string> vList;
        if( !getConfigList( "ALL_COMPONENTS", vList ) )
        {
            return false;
        }
        //Disable all in list
        vector<string>::iterator vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
    }
    else if( stOption.compare("--disable-all") == 0 )
    {
        //Disable ALL_COMPONENTS
        vector<string> vList;
        if( !getConfigList( "ALL_COMPONENTS", vList ) )
        {
            return false;
        }
        //Disable all in list
        vector<string>::iterator vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
        //Disable LIBRARY_LIST
        vList.resize(0);
        if( !getConfigList( "LIBRARY_LIST", vList ) )
        {
            return false;
        }
        //Disable all in list
        vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
        //Disable PROGRAM_LIST
        vList.resize(0);
        if( !getConfigList( "PROGRAM_LIST", vList ) )
        {
            return false;
        }
        //Disable all in list
        vitValues = vList.begin( );
        for( vitValues; vitValues<vList.end( ); vitValues++ )
        {
            toggleConfigValue( *vitValues, false );
        }
    }
    else if( stOption.find("--toolchain") == 0 )
    {
        //A tool chain has been specified
        string sToolChain = stOption.substr( 12 );
        if( sToolChain.compare("msvc") == 0 )
        {
            //Dont disable inline as the configure header will auto header guard it our anyway. This allows for changing on the fly afterwards
        }
        else if ( sToolChain.compare("icl") == 0 )
        {
            //This is the default so dont have to do anything
            // Inline asm by default is turned on
        }
        else
        {
            cout << "  Error: Unknown toolchain option (" << sToolChain << ")" << endl;
            cout << "  Excepted toolchains (msvc, icl)" << endl;
            return false;
        }
        m_sToolchain = sToolChain;
    }
    else
    {
        bool bEnable;
        string sOption;
        if( stOption.find("--enable-") == 0 )
        {
            bEnable = true;
            //Find remainder of option
            sOption = stOption.substr( 9 );
        }
        else if( stOption.find("--disable-") == 0 )
        {
            bEnable = false;
            //Find remainder of option
            sOption = stOption.substr( 10 );
        }
        else
        {
            cout << "  Error: Unknown command line option (" << stOption << ")" << endl;
            return false;
        }
        //Replace any '-'s with '_'
        replace( sOption.begin(), sOption.end(), '-', '_' );
        //Check and make sure that a reserved item is not being changed
        vector<string> vReservedItems;
        buildReservedValues( vReservedItems );
        vector<string>::iterator vitTemp = vReservedItems.begin( );
        for( vitTemp; vitTemp<vReservedItems.end(); vitTemp++ )
        {
            if( vitTemp->compare( sOption ) == 0 )
            {
                cout << "  Warning: Reserved option (" << sOption << ") was passed in command line option (" << stOption << ")" << endl;
                cout << "         This option is reserved and will be ignored" << endl;
                return true;
            }
        }

        uint uiStartPos = sOption.find('=');
        if( uiStartPos != string::npos )
        {
            //Find before the =
            string sList = sOption.substr( 0, uiStartPos );
            //The actual element name is suffixed by list name (all after the =)
            sOption = sOption.substr( uiStartPos + 1 ) + "_" + sList;
            //Get the config element
            ValuesList::iterator vitOption = getConfigOption( sOption );
            if( vitOption == m_vConfigValues.end( ) )
            {
                cout << "  Error: Unknown option (" << sOption << ") in command line option (" << stOption << ")" << endl;
                return false;
            }
            toggleConfigValue( sOption, bEnable );
        }
        else
        {
            //Check if the option is a component
            vector<string> vList;
            getConfigList( "COMPONENT_LIST", vList );
            vector<string>::iterator vitComponent = find( vList.begin(), vList.end(), sOption );
            if( vitComponent != vList.end() )
            {
                //This is a component
                string sOption2 = sOption.substr( 0, sOption.length()-1 ); //Need to remove the s from end
                //Get the specific list
                vList.resize( 0 );
                transform( sOption2.begin( ), sOption2.end( ), sOption2.begin( ), ::toupper );
                getConfigList( sOption2 + "_LIST", vList );
                for( vitComponent = vList.begin(); vitComponent<vList.end(); vitComponent++ )
                {
                    toggleConfigValue( *vitComponent, bEnable );
                }
            }
            else
            {
                //If not one of above components then check if it exists as standalone option
                ValuesList::iterator vitOption = getConfigOption( sOption );
                if( vitOption == m_vConfigValues.end( ) )
                {
                    cout << "  Error: Unknown option (" << sOption << ") in command line option (" << stOption << ")" << endl;
                    return false;
                }
            }
            toggleConfigValue( sOption, bEnable );
        }
    }
    //Add to the internal configuration variable
    ValuesList::iterator vitOption = m_vFixedConfigValues.begin( );
    for( vitOption; vitOption < m_vFixedConfigValues.end( ); vitOption++ )
    {
        if( vitOption->m_sOption.compare( m_sProjectName+"_CONFIGURATION" ) == 0 )
        {
            break;
        }
    }
    vitOption->m_sValue.resize( vitOption->m_sValue.length()-1 ); //Remove trailing "
    if( vitOption->m_sValue.length() > 2 )
    {
        vitOption->m_sValue += ' ';
    }
    vitOption->m_sValue += stOption + "\"";
    return true;
}

bool configGenerator::outputConfig( )
{
    cout << "  Outputting config.h..." << endl;
    //Correct license variables
    if( getConfigOption("version3")->m_sValue.compare( "1" ) == 0 )
    {
        if( getConfigOption("gpl")->m_sValue.compare( "1" ) == 0 )
        {
            fastToggleConfigValue( "gplv3", true );
        }
        else
        {
            fastToggleConfigValue( "lgplv3", true );
        }
    }
    
    //Perform full check of all config values
    ValuesList::iterator vitOption = m_vConfigValues.begin( );
    for( vitOption; vitOption < m_vConfigValues.end( ); vitOption++ )
    {
        if( !passDependencyCheck( vitOption ) )
        {
            return false;
        }
    }

    //Optimise the config values. Based on user input different encoders/decoder can be disabled as there are now better inbuilt alternatives
    //  Must occur after above dependency check so that optimized defaults are not incorrectly turned off based on an input that would otherwiie be disabled
    OptimisedConfigList mOptimisedDisables;
    buildOptimisedDisables( mOptimisedDisables );
    //Check everything that is disabled based on current configuration
    OptimisedConfigList::iterator vitDisable = mOptimisedDisables.begin( );
    bool bDisabledOpt = false;
    for( vitDisable; vitDisable != mOptimisedDisables.end( ); vitDisable++ )
    {
        if( getConfigOption( vitDisable->first )->m_sValue.compare( "1" ) == 0 )
        {
            //Disable unneeded items
            vector<string>::iterator vitOptions = vitDisable->second.begin( );
            for( vitOptions; vitOptions < vitDisable->second.end( ); vitOptions++ )
            {
                bDisabledOpt = true;
                toggleConfigValue( *vitOptions, false );
            }
        }
    }

    //It may be possible that the above optimzation pass disables some dependencies of other options. 
    // If this happens then a full recheck is performed
    if( bDisabledOpt )
    {
        vitOption = m_vConfigValues.begin( );
        for( vitOption; vitOption < m_vConfigValues.end( ); vitOption++ )
        {
            if( !passDependencyCheck( vitOption ) )
            {
                return false;
            }
        }
    }
    
    //Open configure output file
    string sConfigFile = "../../config.h";
    ofstream ofConfigureFile( sConfigFile );
    if( !ofConfigureFile.is_open( ) )
    {
        cout << "  Error: failed opening output configure file (" << sConfigFile << ")" << endl;
        return false;
    }

    //Output header guard
    ofConfigureFile << "/* Automatically generated by SMP project_generate - do not modify! */" << endl;
    ofConfigureFile << "#ifndef " << m_sProjectName << "_CONFIG_H" << endl;
    ofConfigureFile << "#define " << m_sProjectName << "_CONFIG_H" << endl;

    //Build inbuilt force replace list
    DefaultValuesList mReplaceList;
    DefaultValuesList mASMReplaceList;
    buildReplaceValues( mReplaceList, mASMReplaceList );

    //Update the license configuration
    vitOption = m_vFixedConfigValues.begin( );
    for( vitOption; vitOption < m_vFixedConfigValues.end( ); vitOption++ )
    {
        if( vitOption->m_sOption.compare( m_sProjectName+"_LICENSE" ) == 0 )
        {
            break;
        }
    }
    if( getConfigOption( "nonfree" )->m_sValue.compare("1") == 0 )
    {
        vitOption->m_sValue = "\"nonfree and unredistributable\"";
    }
    else if( getConfigOption( "gplv3" )->m_sValue.compare("1") == 0 )
    {
        vitOption->m_sValue = "\"GPL version 3 or later\"";
    }
    else if( getConfigOption( "lgplv3" )->m_sValue.compare("1") == 0 )
    {
        vitOption->m_sValue = "\"LGPL version 3 or later\"";
    }
    else if( getConfigOption( "gpl" )->m_sValue.compare("1") == 0 )
    {
        vitOption->m_sValue = "\"GPL version 2 or later\"";
    }
    else
    {
        vitOption->m_sValue = "\"LGPL version 2.1 or later\"";
    }    

    //Output all fixed config options
    vitOption = m_vFixedConfigValues.begin( );
    for( vitOption; vitOption < m_vFixedConfigValues.end( ); vitOption++ )
    {
        //Check for forced replacement (only if attribute is not disabled)
        if( ( vitOption->m_sValue.compare("0") != 0 ) && ( mReplaceList.find( vitOption->m_sOption ) != mReplaceList.end() ) )
        {
            ofConfigureFile << mReplaceList[vitOption->m_sOption] << endl;
        }
        else
        {
            ofConfigureFile << "#define " << vitOption->m_sOption << " " << vitOption->m_sValue << endl;
        }
    }

    //Open asm configure output file
    sConfigFile = "../../config.asm";
    ofstream ofASMConfigureFile( sConfigFile );
    if( !ofASMConfigureFile.is_open( ) )
    {
        cout << "  Error: failed opening output asm configure file (" << sConfigFile << ")" << endl;
        return false;
    }

    //Output all internal options
    vitOption = m_vConfigValues.begin( );
    for( vitOption; vitOption < m_vConfigValues.begin( )+m_uiConfigValuesEnd; vitOption++ )
    {
        string sTagName = vitOption->m_sPrefix + vitOption->m_sOption;
        //Check for forced replacement (only if attribute is not disabled)
        if( ( vitOption->m_sValue.compare("0") != 0 ) && ( mReplaceList.find( sTagName ) != mReplaceList.end() ) )
        {
            ofConfigureFile << mReplaceList[sTagName] << endl;
        }
        else
        {
            ofConfigureFile << "#define " << sTagName << " " << vitOption->m_sValue << endl;
        }
        if( ( vitOption->m_sValue.compare("0") != 0 ) && ( mASMReplaceList.find( sTagName ) != mASMReplaceList.end() ) )
        {
            ofASMConfigureFile << mASMReplaceList[sTagName] << endl;
        }
        else
        {
            ofASMConfigureFile << "%define " << sTagName << " " << vitOption->m_sValue << endl;
        }
    }
    
    //Output end header guard
    ofConfigureFile << "#endif /* " << m_sProjectName << "_CONFIG_H */" << endl;
    //Close output files
    ofConfigureFile.close();
    ofASMConfigureFile.close();


    //Output avconfig.h
    cout << "  Outputting avconfig.h..." << endl;
    string sAVConfigFile = "../../libavutil/avconfig.h";
    ofstream ofAVConfigFile( sAVConfigFile );
    if( !ofAVConfigFile.is_open( ) )
    {
        cout << "  Error: Failed opening output avconfig file (" << sAVConfigFile << ")" << endl;
        return false;
    }

    //Output header guard
    ofAVConfigFile << "/* Automatically generated by SMP project_generate - do not modify! */" << endl;
    ofAVConfigFile << "#ifndef AVUTIL_AVCONFIG_H" << endl;
    ofAVConfigFile << "#define AVUTIL_AVCONFIG_H" << endl;

    //avconfig.h currently just uses HAVE_LIST_PUB to define its values
    vector<string> vAVConfigList;
    if( !getConfigList( "HAVE_LIST_PUB", vAVConfigList ) )
    {
        cout << "  Error: Failed finding HAVE_LIST_PUB needed for avconfig.h generation" << endl;
        return false;
    }
    for( vector<string>::iterator vitAVC=vAVConfigList.begin(); vitAVC<vAVConfigList.end(); vitAVC++ )
    {
        ValuesList::iterator vitOption = getConfigOption( *vitAVC );
        ofAVConfigFile << "#define " << "AV_HAVE_" << vitOption->m_sOption << " " << vitOption->m_sValue << endl;
    }
    ofAVConfigFile << "#endif /* AVUTIL_AVCONFIG_H */" << endl;
    ofAVConfigFile.close( );


    //Output ffversion.h
    cout << "  Outputting ffversion.h..." << endl;
    //Open VERSION file and get version string
    string sVersionDefFile = "../../../RELEASE";
    ifstream ifVersionDefFile( sVersionDefFile );
    if( !ifVersionDefFile.is_open( ) )
    {
        cout << "  Error: Failed opening output version file (" << sVersionDefFile << ")" << endl;
        return false;
    }
    //Load first line into string
    string sVersion;
    getline( ifVersionDefFile, sVersion );
    ifVersionDefFile.close( );
    //Open output file
    string sVersionFile = "../../libavutil/ffversion.h";
    ofstream ofVersionFile( sVersionFile );
    if( !ofVersionFile.is_open( ) )
    {
        cout << "  Error: Failed opening output version file (" << sVersionFile << ")" << endl;
        return false;
    }
    //Output info
    ofVersionFile << "#ifndef AVUTIL_FFVERSION_H\n#define AVUTIL_FFVERSION_H\n#define FFMPEG_VERSION \"";
    ofVersionFile << sVersion;
    ofVersionFile << "\"\n#endif /* AVUTIL_FFVERSION_H */" << endl;

    ofVersionFile.close( );

    return true;
}

bool configGenerator::getConfigList( const string & sList, vector<string> & vReturn, bool bForce, uint uiCurrentFilePos )
{
    //Find List name in file (searches backwards so that it finds the closest definition to where we currently are)
    //   This is in case a list is redefined
    uint uiStart = m_sConfigureFile.rfind( sList+"=", uiCurrentFilePos );
    //Need to ensure this is the correct list
    while( ( uiStart != string::npos  ) && ( m_sConfigureFile.at( uiStart-1 ) != '\n' ) )
    {
        uiStart = m_sConfigureFile.rfind( sList+"=", uiStart-1 );
    }
    if( uiStart == string::npos )
    {
        if( bForce )
        {
            cout << "  Error: Failed finding config list (" << sList << ")" << endl;
        }
        return false;
    }
    uiStart += sList.length( ) + 1;
    //Check if this is a list or a function
    char cEndList = '\n';
    if( m_sConfigureFile.at(uiStart) == '"' )
    {
        cEndList = '"';
        ++uiStart;
    }
    else if( m_sConfigureFile.at(uiStart) == '\'' )
    {
        cEndList = '\'';
        ++uiStart;
    }
    //Get start of tag
    uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiStart );
    while( m_sConfigureFile.at(uiStart) != cEndList )
    {
        //Check if this is a function
        uint uiEnd;
        if( ( m_sConfigureFile.at(uiStart) == '$' ) && ( m_sConfigureFile.at(uiStart+1) == '(' ) )
        {
            //Skip $(
            uiStart += 2;
            //Get function name
            uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStart+1 );
            string sFunction = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
            //Check if this is a known function
            if( sFunction.compare( "find_things" ) == 0 )
            {
                //Get first parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd+1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStart+1 );
                string sParam1 = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
                //Get second parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd+1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStart+1 );
                string sParam2 = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
                //Get file name
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd+1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace+")", uiStart+1 );
                string sParam3 = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
                //Call function find_things
                if( !passFindThings( sParam1, sParam2, sParam3, vReturn ) )
                {
                    return false;
                }
                //Make sure the closing ) is not included
                uiEnd = ( m_sConfigureFile.at(uiEnd) == ')' )? uiEnd+1 : uiEnd;
            }
            else if( sFunction.compare( "add_suffix" ) == 0 )
            {
                //Get first parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd+1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStart+1 );
                string sParam1 = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
                //Get second parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd+1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace+")", uiStart+1 );
                string sParam2 = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
                //Call function add_suffix
                if( !passAddSuffix( sParam1, sParam2, vReturn ) )
                {
                    return false;
                }
                //Make sure the closing ) is not included
                uiEnd = ( m_sConfigureFile.at(uiEnd) == ')' )? uiEnd+1 : uiEnd;
            }
            else if( sFunction.compare( "filter_out" ) == 0 )
            {
                //This should filter out occurrance of first parameter from the list passed in the second
                uint uiStartSearch = uiStart - sList.length() - 5; //ensure search is before current instance of list
                //Get first parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd + 1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace, uiStart + 1 );
                string sParam1 = m_sConfigureFile.substr( uiStart, uiEnd - uiStart );
                //Get second parameter
                uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd + 1 );
                uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace + ")", uiStart + 1 );
                string sParam2 = m_sConfigureFile.substr( uiStart, uiEnd - uiStart );
                //Call function add_suffix
                if( !passFilterOut( sParam1, sParam2, vReturn, uiStartSearch ) )
                {
                    return false;
                }
                //Make sure the closing ) is not included
                uiEnd = ( m_sConfigureFile.at( uiEnd ) == ')' ) ? uiEnd + 1 : uiEnd;
            }
            else
            {
                cout << "  Error: Unknown list function (" << sFunction << ") found in list (" << sList << ")" << endl;
                return false;
            }
        }
        else
        {
            uiEnd = m_sConfigureFile.find_first_of( m_sWhiteSpace+cEndList, uiStart+1 );
            //Get the tag
            string sTag = m_sConfigureFile.substr( uiStart, uiEnd-uiStart );
            //Check the type of tag
            if( sTag.at(0) == '$' )
            {
                //Strip the identifier
                sTag.erase( 0, 1 );
                //Recursively pass
                if( !getConfigList( sTag, vReturn, bForce, uiEnd ) )
                {
                    return false;
                }
            }
            else
            {
                //Directly add the identifier
                vReturn.push_back( sTag );
            }
        }
        uiStart = m_sConfigureFile.find_first_not_of( m_sWhiteSpace, uiEnd );
        //If this is not specified as a list then only a '\' will allow for more than 1 line
        if( ( cEndList == '\n' ) && ( m_sConfigureFile.at(uiStart) != '\\' ) )
        {
            break;
        }
    }
    return true;
}

bool configGenerator::passFindThings( const string & sParam1, const string & sParam2, const string & sParam3, vector<string> & vReturn )
{
    //Need to find and open the specified file
    string sFile = "../../../" + sParam3;
    ifstream ifSourceFile( sFile );
    if( !ifSourceFile.is_open( ) )
    {
        cout << "  Error: failed opening file (" << sFile << ") in find_things" << endl;
        return false;
    }

    //Load whole file into string
    string sFindFile;
    ifSourceFile.seekg( 0, ifSourceFile.end );
    sFindFile.resize( (uint)ifSourceFile.tellg( ) );
    ifSourceFile.seekg( 0, ifSourceFile.beg );
    ifSourceFile.read( &sFindFile[0], sFindFile.size() );
    ifSourceFile.close( );

    //Find the search pattern in the file
    string sParam1Upper = sParam1;
    transform( sParam1Upper.begin(), sParam1Upper.end(), sParam1Upper.begin(), ::toupper);
    sParam1Upper = "_" + sParam1Upper;
    uint uiStart = sFindFile.find( sParam2 );
    while( uiStart != string::npos )
    {
        //Find the start of the tag (also as ENCDEC should be treated as both DEC+ENC we skip that as well)
        uiStart = sFindFile.find_first_of( m_sWhiteSpace+"(", uiStart+1 );
        //Skip any filling white space
        uiStart = sFindFile.find_first_not_of( " \t", uiStart );
        //Check if valid
        if( sFindFile.at(uiStart) != '(' )
        {
            //Get next
            uiStart = sFindFile.find( sParam2, uiStart+1 );
            continue;
        }
        ++uiStart;
        //Find end of tag
        uint uiEnd = sFindFile.find_first_of( m_sWhiteSpace+",);", uiStart );
        if( sFindFile.at(uiEnd) != ',' )
        {
            //Get next
            uiStart = sFindFile.find( sParam2, uiEnd+1 );
            continue;
        }
        //Get the tag string
        string sTag = sFindFile.substr( uiStart, uiEnd-uiStart );
        //Check to make sure this is a definition not a macro declaration
        if( sTag.compare( "X" ) == 0 )
        {
            //Get next
            uiStart = sFindFile.find( sParam2, uiEnd+1 );
            continue;
        }
        //Get second tag
        uiStart = sFindFile.find_first_not_of( " \t", uiEnd+1 );
        uiEnd = sFindFile.find_first_of( m_sWhiteSpace+",);", uiStart );
        if( ( sFindFile.at(uiEnd) != ')' ) && ( sFindFile.at(uiEnd) != ',' ) )
        {
            //Get next
            uiStart = sFindFile.find( sParam2, uiEnd+1 );
            continue;
        }
        string sTag2 = sFindFile.substr( uiStart, uiEnd-uiStart );
        //Check that both tags match
        transform( sTag2.begin(), sTag2.end(), sTag2.begin(), ::toupper);
        if( sTag2.compare( sTag ) != 0 )
        {
            //Get next
            uiStart = sFindFile.find( sParam2, uiEnd+1 );
            continue;
            //This is somewhat incorrect as the official configuration will always take the second tag
            //  and create a config option out of it. This is actually incorrect as the source code itself
            //  only uses the first parameter as the config option.
        }
        sTag = sTag + sParam1Upper;
        //Add the new value to list
        vReturn.push_back( sTag );
        //Get next
        uiStart = sFindFile.find( sParam2, uiEnd+1 );
    }
    return true;
}

bool configGenerator::passAddSuffix( const string & sParam1, const string & sParam2, vector<string> & vReturn, uint uiCurrentFilePos )
{
    //Convert the first parameter to upper case
    string sParam1Upper = sParam1;
    transform( sParam1Upper.begin(), sParam1Upper.end(), sParam1Upper.begin(), ::toupper);
    //Erase the $ from variable
    string sParam2Cut = sParam2.substr( 1, sParam2.length()-1 );
    //Just call getConfigList
    vector<string> vTemp;
    if( getConfigList( sParam2Cut, vTemp, true, uiCurrentFilePos ) )
    {
        //Update with the new suffix and add to the list
        vector<string>::iterator vitList = vTemp.begin( );
        for( vitList; vitList<vTemp.end( ); vitList++ )
        {
            vReturn.push_back( *vitList + sParam1Upper );
        }
        return true;
    }
    return false;
}

bool configGenerator::passFilterOut( const string & sParam1, const string & sParam2, vector<string> & vReturn, uint uiCurrentFilePos )
{
    //Remove the "'" from the front and back of first parameter
    string sParam1Cut = sParam1.substr( 1, sParam1.length( ) - 2 );
    //Erase the $ from variable2
    string sParam2Cut = sParam2.substr( 1, sParam2.length( ) - 1 );
    //Get the list
    if( getConfigList( sParam2Cut, vReturn, true, uiCurrentFilePos ) )
    {
        vector<string>::iterator vitCheckItem = vReturn.begin( );
        for( vitCheckItem; vitCheckItem<vReturn.end( ); vitCheckItem++ )
        {
            if( vitCheckItem->compare( sParam1Cut ) == 0 )
            {
                vReturn.erase( vitCheckItem );
                //assume only appears once in list
                break;
            }
        }
        return true;
    }
    return false;
}

bool configGenerator::passConfigList( const string & sPrefix, const string & sSuffix, const string & sList )
{
    vector<string> vList;
    if( getConfigList( sList, vList ) )
    {
        //Loop through each member of the list and add it to internal list
        vector<string>::iterator vitList = vList.begin( );
        for( vitList; vitList < vList.end( ); vitList++ )
        {
            //Directly add the identifier
            string sTag = *vitList;
            transform( sTag.begin(), sTag.end(), sTag.begin(), ::toupper);
            sTag = sTag + sSuffix;
            m_vConfigValues.push_back( ConfigPair( sTag, sPrefix, "" ) );
        }
        return true;
    }
    return false;
}

bool configGenerator::fastToggleConfigValue( const string & sOption, bool bEnable )
{
    //Simply find the element in the list and change its setting
    string sOptionUpper = sOption; //Ensure it is in upper case
    transform( sOptionUpper.begin( ), sOptionUpper.end( ), sOptionUpper.begin( ), ::toupper );
    //Find in internal list
    bool bRet = false;
    ValuesList::iterator vitOption = m_vConfigValues.begin( );
    for( vitOption; vitOption<m_vConfigValues.end( ); vitOption++ ) //Some options appear more than once with different prefixes
    {
        if( vitOption->m_sOption.compare( sOptionUpper ) == 0 )
        {
            vitOption->m_sValue = ( bEnable ) ? "1" : "0";
            bRet = true;
        }
    }
    return bRet;
}

bool configGenerator::toggleConfigValue( const string & sOption, bool bEnable, bool bRecursive )
{
    string sOptionUpper = sOption; //Ensure it is in upper case
    transform( sOptionUpper.begin( ), sOptionUpper.end( ), sOptionUpper.begin( ), ::toupper );
    //Find in internal list
    bool bRet = false;
    ValuesList::iterator vitOption = m_vConfigValues.begin( );
    for( vitOption; vitOption<m_vConfigValues.end( ); vitOption++ ) //Some options appear more than once with different prefixes
    {
        if( vitOption->m_sOption.compare( sOptionUpper ) == 0 )
        {
            bRet = true;
            if( !vitOption->m_bLock )
            {
                if( bEnable && ( vitOption->m_sValue.compare( "1" ) != 0 ) )
                {
                    //Lock the item to prevent cyclic conditions
                    vitOption->m_bLock = true;

                    //Need to convert the name to lower case
                    string sOptionLower = sOption;
                    transform( sOptionLower.begin( ), sOptionLower.end( ), sOptionLower.begin( ), ::tolower );
                    string sCheckFunc = sOptionLower + "_select";
                    vector<string> vCheckList;
                    if( getConfigList( sCheckFunc, vCheckList, false ) )
                    {
                        vector<string>::iterator vitCheckItem = vCheckList.begin( );
                        for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
                        {
                            toggleConfigValue( *vitCheckItem, true, true );
                        }
                    }

                    //If enabled then all of these should then be enabled
                    sCheckFunc = sOptionLower + "_suggest";
                    vCheckList.resize( 0 );
                    if( getConfigList( sCheckFunc, vCheckList, false ) )
                    {
                        vector<string>::iterator vitCheckItem = vCheckList.begin( );
                        for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
                        {
                            toggleConfigValue( *vitCheckItem, true, true ); //Weak check
                        }
                    }

                    //Check for any hard dependencies that must be enabled
                    vector<string> vForceEnable;
                    buildForcedEnables( sOptionLower, vForceEnable );
                    vector<string>::iterator vitForcedItem = vForceEnable.begin( );
                    for( vitForcedItem; vitForcedItem<vForceEnable.end( ); vitForcedItem++ )
                    {
                        toggleConfigValue( *vitForcedItem, true, true );
                    }

                    //Unlock item
                    vitOption->m_bLock = false;
                }
                else if( !bEnable && ( vitOption->m_sValue.compare( "0" ) != 0 ) )
                {
                    //Need to convert the name to lower case
                    string sOptionLower = sOption;
                    transform( sOptionLower.begin( ), sOptionLower.end( ), sOptionLower.begin( ), ::tolower );
                    //Check for any hard dependencies that must be disabled
                    vector<string> vForceDisable;
                    buildForcedDisables( sOptionLower, vForceDisable );
                    vector<string>::iterator vitForcedItem = vForceDisable.begin( );
                    for( vitForcedItem; vitForcedItem<vForceDisable.end( ); vitForcedItem++ )
                    {
                        toggleConfigValue( *vitForcedItem, false, true );
                    }
                }
                //Change the items value
                vitOption->m_sValue = ( bEnable ) ? "1" : "0";
            }
        }
    }
    if( !bRet )
    {
        if( bRecursive )
        {
            //Some options are passed in recursively that do not exist in internal list
            // However there dependencies should still be processed
            string sOptionUpper = sOption;
            transform( sOptionUpper.begin(), sOptionUpper.end(), sOptionUpper.begin(), ::toupper);
            m_vConfigValues.push_back( ConfigPair( sOptionUpper, "", "" ) );
            cout << "  Warning: Unlisted config dependency found (" << sOption << ")" << endl;
            //Fix iterator in case of realloc
            vitOption = m_vConfigValues.end() - 1;
        }
        else
        {
            cout << "  Error: Unknown config option (" << sOption << ")" << endl;
            return false;
        }
    }    
    return true;
}

configGenerator::ValuesList::iterator configGenerator::getConfigOption( const string & sOption )
{
    //Ensure it is in upper case
    string sOptionUpper = sOption;
    transform( sOptionUpper.begin(), sOptionUpper.end(), sOptionUpper.begin(), ::toupper);
    //Find in internal list
    ValuesList::iterator vitValues = m_vConfigValues.begin( );
    for( vitValues; vitValues<m_vConfigValues.end( ); vitValues++ )
    {
        if( vitValues->m_sOption.compare( sOptionUpper ) == 0 )
        {
            return vitValues;
        }
    }
    return vitValues;
}

configGenerator::ValuesList::iterator configGenerator::getConfigOptionPrefixed( const string & sOption )
{
    //Ensure it is in upper case
    string sOptionUpper = sOption;
    transform( sOptionUpper.begin(), sOptionUpper.end(), sOptionUpper.begin(), ::toupper);
    //Find in internal list
    ValuesList::iterator vitValues = m_vConfigValues.begin( );
    for( vitValues; vitValues<m_vConfigValues.end( ); vitValues++ )
    {
        if( sOptionUpper.compare( vitValues->m_sPrefix + vitValues->m_sOption ) == 0 )
        {
            return vitValues;
        }
    }
    return vitValues;
}

bool configGenerator::passDependencyCheck( const ValuesList::iterator vitOption )
{
    //Need to convert the name to lower case
    string sOptionLower = vitOption->m_sOption;
    transform( sOptionLower.begin(), sOptionLower.end(), sOptionLower.begin(), ::tolower);

    //Get list of additional dependencies
    DependencyList mAdditionalDependencies;
    buildAdditionalDependencies( mAdditionalDependencies );    

    //Check if disabled
    if( vitOption->m_sValue.compare("1") != 0 )
    {
        //Enabled if any of these
        string sCheckFunc = sOptionLower + "_if_any";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                //Check if this is a not !
                bool bToggle = false;
                if( vitCheckItem->at(0) == '!' )
                {
                    vitCheckItem->erase(0);
                    bToggle = true;
                }
                bool bEnabled;
                ValuesList::iterator vitTemp = getConfigOption( *vitCheckItem );
                if( vitTemp == m_vConfigValues.end( ) )
                {
                    DependencyList::iterator mitDep = mAdditionalDependencies.find( *vitCheckItem  );
                    if( mitDep == mAdditionalDependencies.end( ) )
                    {
                        cout << "  Warning: Unknown option in ifa dependency (" << *vitCheckItem << ") for option (" << sOptionLower << ")" << endl;
                        continue;
                    }
                    bEnabled = mitDep->second ^ bToggle;
                }
                else
                {
                    //Check if this variable has been initialized already
                    if( vitTemp > vitOption )
                    {
                        if( !passDependencyCheck( vitTemp ) )
                        {
                            return false;
                        }
                    }
                    bEnabled = ( vitTemp->m_sValue.compare("1") == 0 ) ^ bToggle;
                }
                if( bEnabled )
                {
                    //If any deps are enabled then enable
                    toggleConfigValue( sOptionLower, true );
                    break;
                }
            }
        }
    }
    //Check if still disabled
    if( vitOption->m_sValue.compare("1") != 0 )
    {
        //Should be enabled if all of these
        string sCheckFunc = sOptionLower + "_if";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            bool bAllEnabled = true;
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                //Check if this is a not !
                bool bToggle = false;
                if( vitCheckItem->at(0) == '!' )
                {
                    vitCheckItem->erase(0);
                    bToggle = true;
                }
                ValuesList::iterator vitTemp = getConfigOption( *vitCheckItem );
                if( vitTemp == m_vConfigValues.end( ) )
                {
                    DependencyList::iterator mitDep = mAdditionalDependencies.find( *vitCheckItem  );
                    if( mitDep == mAdditionalDependencies.end( ) )
                    {
                        cout << "  Warning: Unknown option in if dependency (" << *vitCheckItem << ") for option (" << sOptionLower << ")" << endl;
                        continue;
                    }
                    bAllEnabled = mitDep->second ^ bToggle;
                }
                else
                {
                    //Check if this variable has been initialized already
                    if( vitTemp > vitOption )
                    {
                        if( !passDependencyCheck( vitTemp ) )
                        {
                            return false;
                        }
                    }
                    bAllEnabled = ( vitTemp->m_sValue.compare("1") == 0 ) ^ bToggle;
                }
                if( !bAllEnabled ){ break; }
            }
            if( bAllEnabled )
            {
                //If all deps are enabled then enable
                toggleConfigValue( sOptionLower, true );
            }
        }
    }
    //Perform dependency check if enabled
    if( vitOption->m_sValue.compare("1") == 0 )
    {
        //The following are the needed dependencies that must be enabled
        string sCheckFunc = sOptionLower + "_deps";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                //Check if this is a not !
                bool bToggle = false;
                if( vitCheckItem->at(0) == '!' )
                {
                    vitCheckItem->erase( 0, 1 );
                    bToggle = true;
                }
                bool bEnabled;
                ValuesList::iterator vitTemp = getConfigOption( *vitCheckItem );
                if( vitTemp == m_vConfigValues.end( ) )
                {
                    DependencyList::iterator mitDep = mAdditionalDependencies.find( *vitCheckItem  );
                    if( mitDep == mAdditionalDependencies.end( ) )
                    {
                        cout << "  Warning: Unknown option in dependency (" << *vitCheckItem << ") for option (" << sOptionLower << ")" << endl;
                        continue;
                    }
                    bEnabled = mitDep->second ^ bToggle;
                }
                else
                {
                    //Check if this variable has been initialized already
                    if( vitTemp > vitOption )
                    {
                        if( !passDependencyCheck( vitTemp ) )
                        {
                            return false;
                        }
                    }
                    bEnabled = ( vitTemp->m_sValue.compare("1") == 0 ) ^ bToggle;
                }
                //If not all deps are enabled then disable
                if( !bEnabled )
                {
                    toggleConfigValue( sOptionLower, false );
                    break;
                }
            }
        }
    }
    //Perform dependency check if still enabled
    if( vitOption->m_sValue.compare("1") == 0 )
    {
        //Any 1 of the following dependencies are needed
        string sCheckFunc = sOptionLower + "_deps_any";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            bool bAnyEnabled = false;
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                //Check if this is a not !
                bool bToggle = false;
                if( vitCheckItem->at(0) == '!' )
                {
                    vitCheckItem->erase(0);
                    bToggle = true;
                }
                ValuesList::iterator vitTemp = getConfigOption( *vitCheckItem );
                if( vitTemp == m_vConfigValues.end( ) )
                {
                    DependencyList::iterator mitDep = mAdditionalDependencies.find( *vitCheckItem  );
                    if( mitDep == mAdditionalDependencies.end( ) )
                    {
                        cout << "  Warning: Unknown option in any dependency (" << *vitCheckItem << ") for option (" << sOptionLower << ")" << endl;
                        continue;
                    }
                    bAnyEnabled = mitDep->second ^ bToggle;
                }
                else
                {
                    //Check if this variable has been initialized already
                    if( vitTemp > vitOption )
                    {
                        if( !passDependencyCheck( vitTemp ) )
                        {
                            return false;
                        }
                    }
                    bAnyEnabled = ( vitTemp->m_sValue.compare("1") == 0 ) ^ bToggle;
                }
                if( bAnyEnabled ){ break; }
            }
            if( !bAnyEnabled )
            {
                //If not a single dep is enabled then disable
                toggleConfigValue( sOptionLower, false );
            }
        }
    }
    //Perform dependency check if still enabled
    if( vitOption->m_sValue.compare("1") == 0 )
    {
        //All select items are enabled when this item is enabled. If one of them has since been disabled then so must this one
        string sCheckFunc = sOptionLower + "_select";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                ValuesList::iterator vitTemp = getConfigOption( *vitCheckItem );
                if( vitTemp == m_vConfigValues.end( ) )
                {
                    cout << "  Warning: Unknown option in select dependency (" << *vitCheckItem << ") for option (" << sOptionLower << ")" << endl;
                    continue;
                }
                //Check if this variable has been initialized already
                if( vitTemp > vitOption )
                {
                    // Enable it if it is not currently initialised
                    if( vitTemp->m_sValue.length( ) == 0 )
                    {
                        string sOptionLower2 = vitTemp->m_sOption;
                        transform( sOptionLower2.begin( ), sOptionLower2.end( ), sOptionLower2.begin( ), ::tolower );
                        toggleConfigValue( sOptionLower2, true );
                    }
                    if( !passDependencyCheck( vitTemp ) )
                    {
                        return false;
                    }
                }
                if( vitTemp->m_sValue.compare("0") == 0 )
                {
                    //If any deps are disabled then disable
                    toggleConfigValue( sOptionLower, false );
                    break;
                }
            }
        }
    }

    //Enable any required deps if still enabled
    if( vitOption->m_sValue.compare("1") == 0 )
    {
        string sCheckFunc = sOptionLower + "_select";
        vector<string> vCheckList;
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                toggleConfigValue( *vitCheckItem, true );
            }
        }

        //If enabled then all of these should then be enabled (if not already forced disabled)
        sCheckFunc = sOptionLower + "_suggest";
        vCheckList.resize(0);
        if( getConfigList( sCheckFunc, vCheckList, false ) )
        {
            vector<string>::iterator vitCheckItem = vCheckList.begin( );
            for( vitCheckItem; vitCheckItem<vCheckList.end( ); vitCheckItem++ )
            {
                //Only enable if not forced to disable
                if( getConfigOption( *vitCheckItem )->m_sValue.compare( "0" ) != 0 )
                {
                    toggleConfigValue( *vitCheckItem, true ); //Weak check
                }
            }
        }
    }
    else
    {
        //Ensure the option is not in an uninitialised state
        toggleConfigValue( sOptionLower, false );
    }
    return true;
}




