/*
 * copyright (c) 2015 Matthew Oliver
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

#include "helperFunctions.h"

#include <iostream>
#include <fstream>
#include <direct.h>

#include <Windows.h>
#include <algorithm>
#include "Shlwapi.h"

namespace project_generate {
bool loadFromFile(const string& sFileName, string & sRetString, bool bBinary)
{
    //Open the input file
    ifstream ifInputFile(sFileName, (bBinary) ? ios_base::in | ios_base::binary : ios_base::in);
    if (!ifInputFile.is_open()) {
        cout << "  Error: Failed opening file (" << sFileName << ")" << endl;
        return false;
    }

    //Load whole file into internal string
    ifInputFile.seekg(0, ifInputFile.end);
    uint uiBufferSize = (uint)ifInputFile.tellg();
    ifInputFile.seekg(0, ifInputFile.beg);
    sRetString.resize(uiBufferSize);
    ifInputFile.read(&sRetString[0], uiBufferSize);
    if (uiBufferSize != (uint)ifInputFile.gcount()) {
        sRetString.resize((uint)ifInputFile.gcount());
    }
    ifInputFile.close();
    return true;
}

bool writeToFile(const string & sFileName, const string & sString)
{
    //Open output file
    ofstream ofOutputFile(sFileName);
    if (!ofOutputFile.is_open()) {
        cout << "  Error: failed opening output file (" << sFileName << ")" << endl;
        return false;
    }

    //Output string to file and close
    ofOutputFile << sString;
    ofOutputFile.close();
    return true;
}

bool copyFile(const string & sSourceFile, const string & sDestinationFile)
{
    ifstream ifSource(sSourceFile, ios::binary);
    if (!ifSource.is_open()) {
        return false;
    }
    ofstream ifDest(sDestinationFile, ios::binary);
    if (!ifDest.is_open()) {
        return false;
    }
    ifDest << ifSource.rdbuf();
    ifSource.close();
    ifDest.close();
    return true;
}

void deleteFile(const string & sDestinationFile)
{
    DeleteFile(sDestinationFile.c_str());
}

void deleteFolder(const string & sDestinationFolder)
{
    string delFolder = sDestinationFolder + '\0';
    SHFILEOPSTRUCT file_op = {NULL, FO_DELETE, delFolder.c_str(), "", FOF_NO_UI, false, 0, ""};
    SHFileOperation(&file_op);
}

string getCopywriteHeader(const string& sDecription)
{
    return "/** " + sDecription + "\n\
 *\n\
 * Permission is hereby granted, free of charge, to any person obtaining a copy\n\
 * of this software and associated documentation files (the \"Software\"), to deal\n\
 * in the Software without restriction, including without limitation the rights\n\
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n\
 * copies of the Software, and to permit persons to whom the Software is\n\
 * furnished to do so, subject to the following conditions:\n\
 *\n\
 * The above copyright notice and this permission notice shall be included in\n\
 * all copies or substantial portions of the Software.\n\
 *\n\
 * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n\
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n\
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL\n\
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n\
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n\
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN\n\
 * THE SOFTWARE.\n\
 */";
}

bool makeDirectory(const string & sDirectory)
{
    int iRet = _mkdir(sDirectory.c_str());
    return ((iRet == 0) || (errno == EEXIST));
}

bool findFile(const string & sFileName, string & sRetFileName)
{
    WIN32_FIND_DATA SearchFile;
    HANDLE SearchHandle = FindFirstFile(sFileName.c_str(), &SearchFile);
    if (SearchHandle != INVALID_HANDLE_VALUE) {
        //Update the return filename
        sRetFileName = SearchFile.cFileName;
        FindClose(SearchHandle);
        replace(sRetFileName.begin(), sRetFileName.end(), '\\', '/');
        return true;
    }
    return false;
}

bool findFiles(const string & sFileSearch, vector<string> & vRetFiles, bool bRecursive)
{
    WIN32_FIND_DATA SearchFile;
    uint uiStartSize = vRetFiles.size();
    string sPath;
    string sSearchTerm = sFileSearch;
    uint uiPos = sSearchTerm.rfind('/');
    if (uiPos != string::npos) {
        ++uiPos;
        sPath = sFileSearch.substr(0, uiPos);
        sSearchTerm = sFileSearch.substr(uiPos);
    }
    HANDLE SearchHandle = FindFirstFile(sFileSearch.c_str(), &SearchFile);
    if (SearchHandle != INVALID_HANDLE_VALUE) {
        //Update the return filename list
        vRetFiles.push_back(sPath + SearchFile.cFileName);
        while (FindNextFile(SearchHandle, &SearchFile) != 0) {
            vRetFiles.push_back(sPath + SearchFile.cFileName);
            replace(vRetFiles.back().begin(), vRetFiles.back().end(), '\\', '/');
        }
        FindClose(SearchHandle);
    }
    //Search all sub directories as well
    if (bRecursive) {
        string sSearch = sPath + "*";
        SearchHandle = FindFirstFile(sSearch.c_str(), &SearchFile);
        if (SearchHandle != INVALID_HANDLE_VALUE) {
            BOOL bCont = TRUE;
            while (bCont == TRUE) {
                if (SearchFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // this is a directory
                    if (strcmp(SearchFile.cFileName, ".") != 0 && strcmp(SearchFile.cFileName, "..") != 0) {
                        string sNewPath = sPath + SearchFile.cFileName + '/' + sSearchTerm;
                        findFiles(sNewPath, vRetFiles);
                    }
                }
                bCont = FindNextFile(SearchHandle, &SearchFile);
            }
            FindClose(SearchHandle);
        }
    }
    return (vRetFiles.size() - uiStartSize) > 0;
}

bool findFolders(const string & sFolderSearch, vector<string>& vRetFolders, bool bRecursive)
{
    WIN32_FIND_DATA SearchFile;
    uint uiStartSize = vRetFolders.size();
    string sPath;
    string sSearchTerm = sFolderSearch;
    uint uiPos = sSearchTerm.rfind('/');
    if (uiPos == (sSearchTerm.length() - 1)) {
        uiPos = sSearchTerm.find_last_not_of('/');
        uiPos = sSearchTerm.rfind('/', uiPos - 1);
    }
    if (uiPos != string::npos) {
        ++uiPos;
        sPath = sFolderSearch.substr(0, uiPos);
        sSearchTerm = sFolderSearch.substr(uiPos);
    }
    HANDLE SearchHandle = FindFirstFile(sFolderSearch.c_str(), &SearchFile);
    if (SearchHandle != INVALID_HANDLE_VALUE) {
        //Update the return filename list
        vRetFolders.push_back(sPath + SearchFile.cFileName);
        while (FindNextFile(SearchHandle, &SearchFile) != 0) {
            if (SearchFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                vRetFolders.push_back(sPath + SearchFile.cFileName);
                replace(vRetFolders.back().begin(), vRetFolders.back().end(), '\\', '/');
            }
        }
        FindClose(SearchHandle);
    }
    //Search all sub directories as well
    if (bRecursive) {
        string sSearch = sPath + "*";
        SearchHandle = FindFirstFile(sSearch.c_str(), &SearchFile);
        if (SearchHandle != INVALID_HANDLE_VALUE) {
            BOOL bCont = TRUE;
            while (bCont == TRUE) {
                if (SearchFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // this is a directory
                    if (strcmp(SearchFile.cFileName, ".") != 0 && strcmp(SearchFile.cFileName, "..") != 0) {
                        string sNewPath = sPath + SearchFile.cFileName + '/' + sSearchTerm;
                        findFolders(sNewPath, vRetFolders);
                    }
                }
                bCont = FindNextFile(SearchHandle, &SearchFile);
            }
            FindClose(SearchHandle);
        }
    }
    return (vRetFolders.size() - uiStartSize) > 0;
}

void makePathsRelative(const string& sPath, const string& sMakeRelativeTo, string& sRetPath)
{
    sRetPath.reserve(MAX_PATH);
    sRetPath.resize(MAX_PATH);
    string sFromT, sToT;
    sFromT.reserve(MAX_PATH);
    sToT.reserve(MAX_PATH);
    GetFullPathName(sPath.c_str(), MAX_PATH, const_cast<char*>(sFromT.data()), NULL);
    GetFullPathName(sMakeRelativeTo.c_str(), MAX_PATH, const_cast<char*>(sToT.data()), NULL);
    PathRelativePathTo(const_cast<char*>(sRetPath.data()), sToT.c_str(), FILE_ATTRIBUTE_DIRECTORY, sFromT.c_str(), FILE_ATTRIBUTE_NORMAL);
    sRetPath.resize(strlen(sRetPath.c_str()));
    replace(sRetPath.begin(), sRetPath.end(), '\\', '/');
}

};