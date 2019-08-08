/**-----------------------------------------------------------------------------

    Example code using MKRZero and UDA1334 audio output

    @file MKRZeroSend.ino

    @brief Create a developer account at https://developers.chirp.io,
    and copy and paste your key, secret and config string for chosen
    protocol into the credentials.h file.

    This example will start sending a random chirp payload on start up.

    The example creates a double buffer for DMA tranfer, and populates
    the second buffer whilst the first is being transmitted and vice versa.
    These buffers are read directly by the I2S peripheral to output
    audio data.

    *Note*: This board is send-only, so cannot be configured to receive
    data due to the minimal memory available on the board

    *Important*: The example will not start until this Serial Monitor is opened.
    To disable this behaviour, comment out the while(!Serial) line.

    Circuit:
      - Arduino MKRZero
      - Adafruit UDA1334

    Alternative Boards:
      - Arduino Vidor 4000
      - Genuino Zero
      - Arduino MKR Fox 1200
      - Arduino MKR1000 WiFi

    Copyright © 2011-2019, Asio Ltd.
    All rights reserved.

  ----------------------------------------------------------------------------*/
#include <Adafruit_ZeroI2S.h>
#include <Adafruit_ZeroDMA.h>

#include "chirp_connect.h"
#include "credentials.h"

#define VOLUME            0.5  // Between 0 and 1

#define NUM_BUFFERS       2
#define BUFFER_SIZE       1024
#define SAMPLE_RATE       44100

// Global variables ------------------------------------------------------------

int buffer[NUM_BUFFERS][BUFFER_SIZE];
short tmpBuffer[BUFFER_SIZE / 2];
uint8_t next, current;

Adafruit_ZeroI2S i2s;
Adafruit_ZeroDMA dma;
DmacDescriptor  *desc;

static chirp_connect_t *chirp = NULL;
static volatile bool dma_complete = true;

// Function definitions --------------------------------------------------------

void dmaCallback(Adafruit_ZeroDMA *dma);
void dmaErrorCallback(Adafruit_ZeroDMA *dma);
void setupDMA(void);
void setupChirp(void);
void sendChirp(void);

// Function declarations -------------------------------------------------------

void setup()
{
  current = 0;

  Serial.begin(115200);
  while(!Serial);  // Wait for Serial monitor before continuing

  setupChirp();
  setupDMA();

  i2s.begin(I2S_32_BIT, SAMPLE_RATE);
  i2s.enableMCLK();
  i2s.enableTx();

  sendChirp();

  ZeroDMAstatus stat = dma.startJob();
  if (stat != DMA_STATUS_OK)
  {
    dma.printStatus(stat);
    Serial.println("Failed to start DMA");
  }
}

void loop()
{
  if (dma_complete) {
    next = (current + 1) % NUM_BUFFERS;

    // Process data in to the next mono buffer
    chirp_connect_error_code_t err = chirp_connect_process_shorts_output(chirp, tmpBuffer, BUFFER_SIZE / 2);
    chirpErrorHandler(err);

    // Copy the data into a stereo buffer for audio output
    for (int i = 0; i < BUFFER_SIZE / 2; i++) {
      int value = tmpBuffer[i] * INT16_MAX;
      buffer[next][i * 2] = value;
      buffer[next][i * 2 + 1] = value;
    }

    dma_complete = false;
  }
}

// Chirp -----------------------------------------------------------------------

void chirpErrorHandler(chirp_connect_error_code_t code)
{
  if (code != CHIRP_CONNECT_OK)
  {
    const char *error_string = chirp_connect_error_code_to_string(code);
    Serial.println(error_string);
    exit(42);
  }
}

void onSendingCallback(void *chirp, uint8_t *payload, size_t length, uint8_t channel)
{
  Serial.println("Sending data...");
}

void onSentCallback(void *chirp, uint8_t *payload, size_t length, uint8_t channel)
{
  Serial.println("Sent data...");
}

void sendChirp()
{
  chirp_connect_error_code_t err; 
  
  char *payload = "hello";
  err = chirp_connect_send(chirp, (uint8_t *)payload, strlen(payload));
  chirpErrorHandler(err);

  Serial.print("Sending data: ");
  Serial.println(payload);
}

void setupChirp()
{
  chirp = new_chirp_connect(CHIRP_APP_KEY, CHIRP_APP_SECRET);
  if (chirp == NULL)
  {
    Serial.println("Chirp initialisation failed.");
    return;
  }

  chirp_connect_error_code_t err = chirp_connect_set_config(chirp, CHIRP_APP_CONFIG);
  chirpErrorHandler(err);

  chirp_connect_callback_set_t callback_set = {
    .on_state_changed = NULL,
    .on_sending = onSendingCallback,
    .on_sent = onSentCallback,
    .on_receiving = NULL,
    .on_received = NULL
  };

  err = chirp_connect_set_callbacks(chirp, callback_set);
  chirpErrorHandler(err);

  err = chirp_connect_set_output_sample_rate(chirp, SAMPLE_RATE);
  chirpErrorHandler(err);

  err = chirp_connect_set_volume(chirp, VOLUME);
  chirpErrorHandler(err);

  err = chirp_connect_start(chirp);
  chirpErrorHandler(err);

  Serial.println("Chirp SDK initialised.");
  Serial.flush();
}

// I2S DMA ---------------------------------------------------------------------

void dmaCallback(Adafruit_ZeroDMA *dma)
{
  current++;
  if (current >= NUM_BUFFERS) {
    current -= NUM_BUFFERS;
  }
  dma->changeDescriptor(desc, buffer[current]);
  ZeroDMAstatus stat = dma->startJob();
  if (stat == DMA_STATUS_OK) {
    dma_complete = true;
  } else {
    Serial.println("ERROR");
  }
}

void dmaErrorCallback(Adafruit_ZeroDMA *dma)
{
  Serial.println("DMAERROR");
}

void setupDMA(void)
{
  Serial.println("Configuring DMA trigger");
  dma.setTrigger(I2S_DMAC_ID_TX_0);
  dma.setAction(DMA_TRIGGER_ACTON_BEAT);

  Serial.println("Allocating DMA channel");
  ZeroDMAstatus stat = dma.allocate();
  dma.printStatus(stat);

  Serial.println("Setting up transfer");
  desc = dma.addDescriptor(
    buffer[current],
    (void *)(&I2S->DATA[0].reg),
    BUFFER_SIZE,
    DMA_BEAT_SIZE_WORD,
    true,
    false
  );

  Serial.println("Adding DMA callbacks");
  dma.setCallback(dmaCallback);
  dma.setCallback(dmaErrorCallback, DMA_CALLBACK_TRANSFER_ERROR);
}
