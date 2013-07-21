/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 2012 Unvanquished Developers

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

#ifndef ROCKETKEYBINDER_H
#define ROCKETKEYBINDER_H

#include <Rocket/Core.h>
#include <Rocket/Core/Element.h>

extern "C"
{
#include "client.h"
}

class RocketKeyBinder : public Rocket::Core::Element
{
public:
	RocketKeyBinder( const Rocket::Core::String &tag ) : Rocket::Core::Element( tag ), dirty_key( false ), waitingForKeypress( false ), team( 0 ), key( -1 )
	{
	}

	void OnAttributeChange( const Rocket::Core::AttributeNameList &changed_attributes )
	{
		if ( changed_attributes.find( "cmd" ) != changed_attributes.end() )
		{
			cmd = GetAttribute( "cmd" )->Get<Rocket::Core::String>();
			dirty_key = true;
		}

		if ( changed_attributes.find( "team" ) != changed_attributes.end() )
		{
			team = Key_GetTeam( GetAttribute( "team" )->Get<Rocket::Core::String>().CString(), "Rocket KeyBinder" );
			dirty_key = true;
		}
	}

	void OnUpdate( void )
	{
		if ( dirty_key && team >= 0 )
		{
			dirty_key = false;
			key = Key_GetKey( cmd.CString(), team );
			SetInnerRML( Key_KeynumToString( key ) );
		}
	}

	void ProcessEvent( Rocket::Core::Event &event )
	{
		if ( event == "click" && event.GetTargetElement() == this )
		{
			waitingForKeypress = true;
			SetInnerRML( "Enter desired key..." );
		}

		else if ( waitingForKeypress && event == "keydown" && event.GetTargetElement() == this )
		{
			int newKey = Rocket_ToQuakeKey( ( Rocket::Core::Input::KeyIdentifier ) event.GetParameter< int >( "key_identifier", 0 ) );

			BindKey( newKey );
		}

		else if ( waitingForKeypress && event == "mouseup" && event.GetTargetElement() == this )
		{
			int button = event.GetParameter<int>( "button", 1 );
			int newKey;
			switch (button)
			{
				case 0: newKey = K_MOUSE1; break;
				case 1: newKey = K_MOUSE3; break;
				case 2: newKey = K_MOUSE2; break;
				default: break;
			}

			BindKey( newKey );
		}
	}

protected:
	void BindKey( int newKey )
	{
		if ( key == newKey ) { return; }

		Key_SetBinding( newKey, team, cmd.CString() );

		if ( key > 0 )
		{
			Key_SetBinding( key, team, NULL );
		}

		key = newKey;
		dirty_key = true;
		waitingForKeypress = false;
	}

private:
	bool dirty_key;
	bool waitingForKeypress;
	int team;
	int key;

	Rocket::Core::String cmd;
};


#endif
