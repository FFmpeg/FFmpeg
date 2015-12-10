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
 * Copyright (c) 2015 Matthew Oliver\n\
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

};