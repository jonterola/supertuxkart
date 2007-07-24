//  $Id$
//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2007 Damien Morel <divdams@free.fr>
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#if HAVE_OPENAL && HAVE_OGGVORBIS

#include <stdexcept>
#ifdef __APPLE__
#  include <OpenAL/al.h>
#else
#  include <AL/al.h>
#endif
#include <AL/alut.h>

#include "music_ogg.hpp"
#include "loader.hpp"
#include "user_config.hpp"

#define BUFFER_SIZE (4096 * 8)


MusicOggStream::MusicOggStream()
{
    //m_oggStream= NULL;
    m_soundBuffers[0]= m_soundBuffers[1]= 0;
    m_soundSource= 0;
    m_pausedMusic= true;
}

//-----------------------------------------------------------------------------
MusicOggStream::~MusicOggStream()
{
    if(stopMusic() == false)
	fprintf(stderr, "WARNING: problems while stopping music.\n");
}

//-----------------------------------------------------------------------------
bool MusicOggStream::load(const char* filename)
{
    if(!release())
    {
        user_config->setMusic(UserConfig::UC_TEMPORARY_DISABLE);
        fprintf(stderr,"Problems oggStream:release. Disabling music.\n");
        return false;
    }

    try
    {
      m_fileName =  loader->getPath(filename);
    }
    catch(std::runtime_error)
    {
	return false;
    }
    oggFile = fopen(m_fileName.c_str(), "rb");

    if (ov_open(oggFile, &m_oggStream, NULL, 0) < 0)
    {
	fclose(oggFile);
        printf("Loading Music: %s failed\n", m_fileName.c_str());
        return false;
    }

    vorbisInfo = ov_info(&m_oggStream, -1);

    if(vorbisInfo->channels == 1)
        nb_channels = AL_FORMAT_MONO16;
    else
        nb_channels = AL_FORMAT_STEREO16;


    alGenBuffers(2, m_soundBuffers);
    if(check() == false)
        return false;;

    alGenSources(1, &m_soundSource);
    if(check() == false)
        return false;

    alSource3f(m_soundSource, AL_POSITION,        0.0, 0.0, 0.0);
    alSource3f(m_soundSource, AL_VELOCITY,        0.0, 0.0, 0.0);
    alSource3f(m_soundSource, AL_DIRECTION,       0.0, 0.0, 0.0);
    alSourcef (m_soundSource, AL_ROLLOFF_FACTOR,  0.0          );
    alSourcei (m_soundSource, AL_SOURCE_RELATIVE, AL_TRUE      );

    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::empty()
{
    int queued= 0;
    alGetSourcei(m_soundSource, AL_BUFFERS_QUEUED, &queued);

    while(queued--)
    {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(m_soundSource, 1, &buffer);

        if(check() == false)
            return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::release()
{
    if (m_fileName == "")
    {
        // nothing is loaded
        return true;
    }

    pauseMusic();
    m_fileName= "";

    empty();
    alDeleteSources(1, &m_soundSource);
    check();
    alDeleteBuffers(2, m_soundBuffers);
    check();
    ov_clear(&m_oggStream);

    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::playMusic()
{
    if(isPlaying())
        return true;

    if(!streamIntoBuffer(m_soundBuffers[0]))
        return false;

    if(!streamIntoBuffer(m_soundBuffers[1]))
        return false;

    alSourceQueueBuffers(m_soundSource, 2, m_soundBuffers);

    alSourcePlay(m_soundSource);
    m_pausedMusic= false;

    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::isPlaying()
{
    ALenum state;
    alGetSourcei(m_soundSource, AL_SOURCE_STATE, &state);

    return (state == AL_PLAYING);
}

//-----------------------------------------------------------------------------
bool MusicOggStream::stopMusic()
{
    return (release());
}

//-----------------------------------------------------------------------------
bool MusicOggStream::pauseMusic()
{
    if (m_fileName == "")
    {
        // nothing is loaded
        return true;
    }

    alSourceStop(m_soundSource);
    m_pausedMusic= true;
    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::resumeMusic()
{
    if (m_fileName == "")
    {
        // nothing is loaded
        return true;
    }

    alSourcePlay(m_soundSource);
    m_pausedMusic= false;
    return true;
}

//-----------------------------------------------------------------------------
void MusicOggStream::update()
{

    if (m_pausedMusic)
    {
        // nothing todo
        return;
    }

    int processed= 0;
    bool active= true;

    alGetSourcei(m_soundSource, AL_BUFFERS_PROCESSED, &processed);

    while(processed--)
    {
        ALuint buffer;

        alSourceUnqueueBuffers(m_soundSource, 1, &buffer);
        if(check() == false)
            return;

        active = streamIntoBuffer(buffer);
        alSourceQueueBuffers(m_soundSource, 1, &buffer);

        if(check() == false)
            return;
    }

    // check for underrun
    if (active)
    {
        // we have data, so we should be playing...
        ALenum state;
        alGetSourcei(m_soundSource, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING)
        {
	    fprintf(stderr,"WARNING: Alsa source state: %d\n", state);
            alGetSourcei(m_soundSource, AL_BUFFERS_PROCESSED, &processed);
            alSourcePlay(m_soundSource);
        }
    }
    else
    {
        // no more data. Seek to beginning -> loop
	ov_time_seek(&m_oggStream, 0);
    }
}

//-----------------------------------------------------------------------------
bool MusicOggStream::streamIntoBuffer(ALuint buffer)
{
    char pcm[BUFFER_SIZE];
#ifdef WORDS_BIGENDIAN
    int  isBigEndian = 1;
#else
    int  isBigEndian = 0;
#endif
    int  size = 0;
    int  portion;
    int  result;

    while(size < BUFFER_SIZE)
    {
        result = ov_read(&m_oggStream, pcm + size, BUFFER_SIZE - size, isBigEndian, 2, 1, &portion);

        if(result > 0)
            size += result;
        else
            if(result < 0)
                throw errorString(result);
            else
                break;
    }

    if(size == 0)
        return false;


    alBufferData(buffer, nb_channels, pcm, size, vorbisInfo->rate);
    check();

    return true;
}

//-----------------------------------------------------------------------------
bool MusicOggStream::check()
{
    int error = alGetError();

    if(error != AL_NO_ERROR)
    {
	fprintf(stderr, "OpenAL error: %d\n", error);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
string MusicOggStream::errorString(int code)
{
    switch(code)
    {
        case OV_EREAD:
            return string("Read error from media.");
        case OV_ENOTVORBIS:
            return string("It is not Vorbis data.");
        case OV_EVERSION:
            return string("Vorbis version mismatch.");
        case OV_EBADHEADER:
            return string("Invalid Vorbis bitstream header.");
        case OV_EFAULT:
            return string("Internal logic fault (bug or heap/stack corruption).");
        default:
            return string("Unknown Vorbis error.");
    }
}

#endif // HAVE_OPENAL && HAVE_OGGVORBIS
