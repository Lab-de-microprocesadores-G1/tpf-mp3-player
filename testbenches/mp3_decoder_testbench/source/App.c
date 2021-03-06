/********************************************************************************
  @file     App.c
  @brief    Application functions
  @author   N. Magliola, G. Davidov, F. Farall, J. Gaytán, L. Kammann, N. Trozzo
 *******************************************************************************/

/*******************************************************************************
 * INCLUDE HEADER FILES
 ******************************************************************************/

#include "board.h"
#include "lib/fatfs/ff.h"
#include "lib/mp3decoder/mp3decoder.h"
#include "drivers/MCAL/gpio/gpio.h"
#include "drivers/MCAL/dac_dma/dac_dma.h"
#include <math.h>
#include <string.h>

/*******************************************************************************
 * CONSTANT AND MACRO DEFINITIONS USING #DEFINE
 ******************************************************************************/

#define FILEPATH 	"440tone.mp3"
#define FRAME_SIZE	2048
#define SAMPLE_RATE	44100

#define DAC_PEAK_VALUE	4096


/*******************************************************************************
 * FUNCTION PROTOTYPES FOR PRIVATE FUNCTIONS WITH FILE LEVEL SCOPE
 ******************************************************************************/

static void updateCallback(uint16_t * frameToUpdate);
static uint16_t mp3ToDac(int16_t sample);


/*******************************************************************************
 * VARIABLES TYPES DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
 * PRIVATE VARIABLES WITH FILE LEVEL SCOPE
 ******************************************************************************/

static bool     init;
static FATFS	fat;
static FRESULT	fr;
static int16_t  decodedBuffer[MP3_DECODED_BUFFER_SIZE + FRAME_SIZE] __attribute__ ((aligned(32)));
static uint16_t samplesDecoded;
static mp3decoder_frame_data_t frameData;
static mp3decoder_result_t res;
static mp3decoder_tag_data_t ID3Tag;

static uint32_t 	availableSamples = 0;
static uint16_t 	frames[2][FRAME_SIZE];
static uint8_t 		channelCount;
static bool 		dmaInterrupted = false;
uint16_t*			bufferToUpdate;

static uint16_t aux1, aux2, huevo;
static int16_t currDiff, maxDiff = 0;
static uint32_t framesDecoded = 0;


/*******************************************************************************
 *******************************************************************************
                        GLOBAL FUNCTION DEFINITIONS
 *******************************************************************************
 ******************************************************************************/

/* Called once at the beginning of the program */
void appInit (void)
{
	init = false;

	/* Default initialization of the board */
	boardInit();

	/* Initialize output DAC_DMA */
	dacdmaInit();
	dacdmaSetBuffers(frames[0], frames[1], FRAME_SIZE, updateCallback);
	dacdmaSetFreq(SAMPLE_RATE);

	/* Config test pins */
	gpioMode(PIN_INTERRUPT, OUTPUT);
	gpioMode(PIN_APP_BLOCK, OUTPUT);
	gpioWrite(PIN_INTERRUPT, LOW);
	gpioWrite(PIN_APP_BLOCK, LOW);

	/* Initialization of the MP3 decoder*/
	MP3DecoderInit();
}

/* Called repeatedly in an infinite loop */
void appRun (void)
{
  if (!init)
  {
	do
	{
		/* Mount the default drive */
		fr = f_mount(&fat, "", 1);
	}
	while (fr == FR_NOT_READY);

	/* Change current directory */
	fr = f_chdir(".3pm");

	init = true;
  }

  if (MP3LoadFile(FILEPATH))
  {
	/* Get ID3v2 information */
	MP3GetTagData(&ID3Tag);

	/* Get info from first frame to be decoded */
	MP3GetNextFrameData(&frameData);

	channelCount = frameData.channelCount;

	while ((availableSamples <  (FRAME_SIZE * channelCount)) && (res == MP3DECODER_NO_ERROR))
	{
		/* Decode mp3 frame */
		res = MP3GetDecodedFrame(decodedBuffer + availableSamples, MP3_DECODED_BUFFER_SIZE, &samplesDecoded);
		availableSamples += samplesDecoded;
		framesDecoded++;

		for (uint16_t i = 0 ; i < availableSamples ; i++)
		{
			if (i > 0)
			{
				aux1 = mp3ToDac(decodedBuffer[channelCount * i]);
				aux2 = mp3ToDac(decodedBuffer[channelCount * (i-1)]);
				currDiff = abs(aux1 - aux2);
				if (currDiff > maxDiff)
				{
					maxDiff = currDiff;
					huevo++;
				}
				if (currDiff >= 2000)
				{
					huevo++;
					huevo--;
				}

			}
		}
	}

	dacdmaStart();

	while (res == MP3DECODER_NO_ERROR)
	{
		if (dmaInterrupted)
		{
			gpioWrite(PIN_APP_BLOCK, HIGH);
			/* if DMA interrupted, copy info to buffer */

			for (uint16_t i = 0 ; i < FRAME_SIZE ; i++)
			{
				bufferToUpdate[i] = mp3ToDac(decodedBuffer[channelCount * i]);

				if (i > 0)
				{
					aux1 = mp3ToDac(decodedBuffer[channelCount * i]);
					aux2 = mp3ToDac(decodedBuffer[channelCount * (i-1)]);
					currDiff = abs(aux1 - aux2);
					if (currDiff > maxDiff)
					{
						maxDiff = currDiff;
						huevo++;
					}
					if (currDiff >= 2000)
					{
						huevo++;
						huevo--;
					}
				}
			}

			/* update available samples in decoded array */
			availableSamples -= FRAME_SIZE * channelCount;

			/* rearrange decoded array to fill info */
			if ( availableSamples > 0 )
			{
				memmove(decodedBuffer, decodedBuffer + FRAME_SIZE * channelCount, availableSamples * sizeof(uint16_t));
			}

			/* now we decode more mp3 frames */

			while( (res == MP3DECODER_NO_ERROR) && (availableSamples < FRAME_SIZE * channelCount) )
			{
				/* Decode mp3 frame */
				res = MP3GetDecodedFrame(decodedBuffer + availableSamples, MP3_DECODED_BUFFER_SIZE, &samplesDecoded);
				availableSamples += samplesDecoded;
				framesDecoded++;

				for (uint16_t i = 0 ; i < availableSamples ; i++)
				{
					if (i > 0)
					{
						aux1 = mp3ToDac(decodedBuffer[channelCount * i]);
						aux2 = mp3ToDac(decodedBuffer[channelCount * (i-1)]);
						currDiff = abs(aux1 - aux2);
						if (currDiff > maxDiff)
						{
							maxDiff = currDiff;
							huevo++;
						}
						if (currDiff >= 2000)
						{
							huevo++;
							huevo--;
						}
					}
				}
			}

			gpioWrite(PIN_APP_BLOCK, LOW);

			/* clear flag */
			dmaInterrupted = false;
		}
	}
	dacdmaStop();

	while(1); // end program

  }
}


/*******************************************************************************
 *******************************************************************************
                        LOCAL FUNCTION DEFINITIONS
 *******************************************************************************/

uint16_t mp3ToDac(int16_t sample)
{
	return sample / 16 + DAC_PEAK_VALUE / 2;
}


static void updateCallback(uint16_t * frameToUpdate)
{
	gpioToggle(PIN_INTERRUPT);

	/* Update pointer */
	bufferToUpdate = frameToUpdate;

	if (dmaInterrupted)
	{
		static int huevo = 0;
		huevo++;
	}
	/* Need more frames */
	dmaInterrupted = true;
}

/*******************************************************************************
 ******************************************************************************/
