/*! @file DarwinActionators.h
    @brief Declaration of Bear actionators class
 
    @class DarwinActionators
    @brief The darwin actionators class
 
    @author Jason Kulk
 
  Copyright (c) 2011 Jason Kulk
 
    This file is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This file is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NUbot.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DARWINACTIONATORS_H
#define DARWINACTIONATORS_H

#include "NUPlatform/NUActionators.h"

class DarwinActionators : public NUActionators
{
public:
    DarwinActionators();
    ~DarwinActionators();
    
private:
    void copyToHardwareCommunications();
    void copyToServos();
    void copyToLeds();
    
private:
    vector<string> m_servo_names;            //!< the names of the available joints (eg HeadYaw, AnklePitch etc) in the Darwin-OP robot
};

#endif

