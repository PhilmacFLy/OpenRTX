/***************************************************************************
 *   Copyright (C) 2022 by Federico Amedeo Izzo IU2NUO,                    *
 *                         Niccolò Izzo IU2KIN,                            *
 *                         Silvano Seva IU2KWO                             *
 *                         Joseph Stephen VK7JS                            *
 *                         Roger Clark  VK3KYY                             *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/
#include <ctype.h>
#include "core/voicePrompts.h"
#include "ui/UIStrings.h"
const uint32_t VOICE_PROMPTS_DATA_MAGIC = 0x5056;//'VP'
const uint32_t VOICE_PROMPTS_DATA_VERSION = 0x1000; // v1000 OpenRTX
#define VOICE_PROMPTS_TOC_SIZE 350
static void getM17Data(int offset,int length);

typedef struct
{
	uint32_t magic;
	uint32_t version;
} voicePromptsDataHeader_t;



const uint32_t VOICE_PROMPTS_FLASH_HEADER_ADDRESS 		= 0x8F400; // todo figure this out for OpenRTX
static uint32_t vpFlashDataAddress;// = VOICE_PROMPTS_FLASH_HEADER_ADDRESS + sizeof(voicePromptsDataHeader_t) + sizeof(uint32_t)*VOICE_PROMPTS_TOC_SIZE ;
// TODO figure out M17 frame equivalent.
// 76 x 27 byte ambe frames
#define M17_DATA_BUFFER_SIZE  2052

bool voicePromptDataIsLoaded = false;
static bool voicePromptIsActive = false;
static int promptDataPosition = -1;
static int currentPromptLength = -1;

#define PROMPT_TAIL  30
static int promptTail = 0;

static uint8_t M17Data[M17_DATA_BUFFER_SIZE];

#define VOICE_PROMPTS_SEQUENCE_BUFFER_SIZE 128

typedef struct
{
	uint16_t  Buffer[VOICE_PROMPTS_SEQUENCE_BUFFER_SIZE];
	int  Pos;
	int  Length;
} vpSequence_t;

static vpSequence_t vpCurrentSequence =
{
	.Pos = 0,
	.Length = 0
};

uint32_t tableOfContents[VOICE_PROMPTS_TOC_SIZE];


void vpCacheInit(void)
{
	voicePromptsDataHeader_t header;

	SPI_Flash_read(VOICE_PROMPTS_FLASH_HEADER_ADDRESS,(uint8_t *)&header,sizeof(voicePromptsDataHeader_t));

	if (vpCheckHeader((uint32_t *)&header))
	{
		voicePromptDataIsLoaded = SPI_Flash_read(VOICE_PROMPTS_FLASH_HEADER_ADDRESS + sizeof(voicePromptsDataHeader_t), (uint8_t *)&tableOfContents, sizeof(uint32_t) * VOICE_PROMPTS_TOC_SIZE);
		vpFlashDataAddress =  VOICE_PROMPTS_FLASH_HEADER_ADDRESS + sizeof(voicePromptsDataHeader_t) + sizeof(uint32_t)*VOICE_PROMPTS_TOC_SIZE ;
	}

}

bool vpCheckHeader(uint32_t *bufferAddress)
{
	voicePromptsDataHeader_t *header = (voicePromptsDataHeader_t *)bufferAddress;

	return ((header->magic == VOICE_PROMPTS_DATA_MAGIC) && (header->version == VOICE_PROMPTS_DATA_VERSION));
}

static void getM17Data(int offset,int length)
{
	if (length <= M17_DATA_BUFFER_SIZE)
	{
		SPI_Flash_read(vpFlashDataAddress + offset, (uint8_t *)&M17Data, length);
	}
}

void vpTick(void)
{
	if (voicePromptIsActive)
	{
		if (promptDataPosition < currentPromptLength)
		{
			//if (wavbuffer_count <= (WAV_BUFFER_COUNT / 2))
			{
//				codecDecode((uint8_t *)&M17Data[promptDataPosition], 3);
				promptDataPosition += 27;
			}

			//soundTickRXBuffer();
		}
		else
		{
			if ( vpCurrentSequence.Pos < (vpCurrentSequence.Length - 1))
			{
				vpCurrentSequence.Pos++;
				promptDataPosition = 0;

				int promptNumber = vpCurrentSequence.Buffer[vpCurrentSequence.Pos];
				currentPromptLength = tableOfContents[promptNumber + 1] - tableOfContents[promptNumber];
				getM17Data(tableOfContents[promptNumber], currentPromptLength);
			}
			else
			{
				// wait for wave buffer to empty when prompt has finished playing

//				if (wavbuffer_count == 0)
				{
					vpTerminate();
				}
			}
		}
	}
	else
	{
		if (promptTail > 0)
		{
			promptTail--;

			if ((promptTail == 0) && trxCarrierDetected() && (trxGetMode() == RADIO_MODE_ANALOG))
			{
				//GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1); // Set the audio path to AT1846 -> audio amp.
			}
		}
	}
}

void vpTerminate(void)
{
	if (voicePromptIsActive)
	{
		//disableAudioAmp(AUDIO_AMP_MODE_PROMPT);

		vpCurrentSequence.Pos = 0;
		//soundTerminateSound();
		//soundInit();
		promptTail = PROMPT_TAIL;

		voicePromptIsActive = false;
	}
}

void vpInit(void)
{
	if (voicePromptIsActive)
	{
		vpTerminate();
	}
	
	vpCurrentSequence.Length = 0;
	vpCurrentSequence.Pos = 0;
}

void vpQueuePrompt(uint16_t prompt)
{
	if (voicePromptIsActive)
	{
		vpInit();
	}
	if (vpCurrentSequence.Length < VOICE_PROMPTS_SEQUENCE_BUFFER_SIZE)
	{
		vpCurrentSequence.Buffer[vpCurrentSequence.Length] = prompt;
		vpCurrentSequence.Length++;
	}
}

// This function spells out a string letter by letter.
void vpQueueString(char *promptString, VoicePromptFlags_T flags)
{
	const char indexedSymbols[] = "%.+-*#!,@:?()~/[]<>=$'`&|_^{}"; // handles must match order of vps.
	const char commonSymbols[] = "%.+-*#"; // handles must match order of vps.
	
	if (voicePromptIsActive)
	{
		vpInit();
	}
	bool announceCommonSymbols = (flags & vpAnnounceCommonSymbols) ? true : false;
	bool announceLessCommonSymbols=(flags & vpAnnounceLessCommonSymbols) ? true : false;
	
	while (*promptString != 0)
	{
		if ((*promptString >= '0') && (*promptString <= '9'))
		{
			vpQueuePrompt(*promptString - '0' + PROMPT_0);
		}
		else if ((*promptString >= 'A') && (*promptString <= 'Z'))
		{
			if (flags&vpAnnounceCaps)
				vpQueuePrompt(PROMPT_CAP);
			if (flags&vpAnnouncePhoneticRendering)
				vpQueuePrompt((*promptString - 'A') + PROMPT_A_PHONETIC);
			else
				vpQueuePrompt(*promptString - 'A' + PROMPT_A);
		}
		else if ((*promptString >= 'a') && (*promptString <= 'z'))
		{
			if (flags&vpAnnouncePhoneticRendering)
				vpQueuePrompt((*promptString - 'a') + PROMPT_A_PHONETIC);
			else
				vpQueuePrompt(*promptString - 'a' + PROMPT_A);
		}
		else if ((*promptString==' ') && (flags&vpAnnounceSpace))
		{
			vpQueuePrompt(PROMPT_SPACE);
		}
		else if (announceCommonSymbols  || announceLessCommonSymbols)
		{
			char* symbolPtr = strchr(indexedSymbols, *promptString);
			if (symbolPtr != NULL)
			{
				bool commonSymbol= strchr(commonSymbols, *symbolPtr) != NULL;
				
				if ((commonSymbol && announceCommonSymbols) || (!commonSymbol && announceLessCommonSymbols))
				{
					vpQueuePrompt(PROMPT_PERCENT+(symbolPtr-indexedSymbols));
				}
				else
				{
					vpQueuePrompt(PROMPT_SILENCE);
				}
			}
			else if (flags&vpAnnounceASCIIValueForUnknownChars)
			{
				int32_t val = *promptString;
				vpQueueLanguageString(&currentLanguage->dtmf_code); // just the word "code" as we don't have character.
				vpQueueInteger(val);
			}
			else
			{
				vpQueuePrompt(PROMPT_SILENCE);
			}
		}
		else
		{
			// otherwise just add silence
			vpQueuePrompt(PROMPT_SILENCE);
		}
		
		promptString++;
	}
}

void vpQueueInteger(int32_t value)
{
	char buf[12] = {0}; // min: -2147483648, max: 2147483647
	itoa(value, buf, 10);
	vpQueueString(buf, 0);
}

// This function looks up a voice prompt corresponding to a string table entry.
// These are stored in the voice data after the voice prompts with no corresponding string table entry, hence the offset calculation:
// NUM_VOICE_PROMPTS + (stringTableStringPtr - currentLanguage->languageName)
void vpQueueStringTableEntry(const char * const *stringTableStringPtr)
{
	if (stringTableStringPtr == NULL)
	{
		return;
	}
	vpQueuePrompt(NUM_VOICE_PROMPTS + (stringTableStringPtr - currentLanguage->languageName));
}

void vpPlay(void)
{
	if ((voicePromptIsActive == false) && (vpCurrentSequence.Length > 0))
	{
		voicePromptIsActive = true;// Start the playback
		int promptNumber = vpCurrentSequence.Buffer[0];

		vpCurrentSequence.Pos = 0;
		
		currentPromptLength = tableOfContents[promptNumber + 1] - tableOfContents[promptNumber];
		getM17Data(tableOfContents[promptNumber], currentPromptLength);
		
//		GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 0);// set the audio mux HR-C6000 -> audio amp
		//enableAudioAmp(AUDIO_AMP_MODE_PROMPT);

		//codecInit(true);
		promptDataPosition = 0;

	}
}

inline bool vpIsPlaying(void)
{
	return (voicePromptIsActive || (promptTail > 0));
}

bool vpHasDataToPlay(void)
{
	return (vpCurrentSequence.Length > 0);
}
