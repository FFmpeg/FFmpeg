
#include "configGenerator.h"
#include "projectGenerator.h"

int main( int argc, char** argv )
{
    cout << "Project generator..." << endl;
    //Pass the input configuration
    projectGenerator ProjectHelper;
    //Check if the input configuration file was specified on the command line
    if( (argc==2) && (string(argv[1]).find("--config-file=")==0) )
    {
        if( !ProjectHelper.m_ConfigHelper.passConfigFile( string(argv[0]) ) )
        {
            system("pause");
            exit( 1 );
        }
    }
    else
    {
        if( !ProjectHelper.m_ConfigHelper.passConfig( ) )
        {
            system("pause");
            exit( 1 );
        }
    }
    //Pass input arguments
    for( int i=1; i<argc; i++ )
    {
        if( !ProjectHelper.m_ConfigHelper.changeConfig( argv[i] ) )
        {
            system("pause");
            exit( 1 );
        }
    }

    //Output config.h and avutil.h
    if( !ProjectHelper.m_ConfigHelper.outputConfig( ) )
    {
        system("pause");
        exit( 1 );
    }

    //Generate desired configuration files
    if( !ProjectHelper.passAllMake( ) )
    {
        system("pause");
        exit( 1 );
    }
    cout << "Completed Successfully" << endl;
#if _DEBUG
    system("pause");
#endif
}