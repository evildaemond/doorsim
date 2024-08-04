#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <Preferences.h>
#include "AsyncJson.h"
#include "ArduinoJson.h"

AsyncWebServer server(80);
Preferences preferences;

// Set the LCD I2C address
LiquidCrystal_I2C lcd(0x20, 20, 4);

// general device settings
bool isCapturing = true;
String MODE; 

// card reader config and variables

// max number of bits
#define MAX_BITS 100
// time to wait for another weigand pulse
#define WEIGAND_WAIT_TIME 3000

// stores all of the data bits
volatile unsigned char databits[MAX_BITS];
volatile unsigned int bitCount = 0;

// stores the last written card's data bits
unsigned char lastWrittenDatabits[MAX_BITS];
unsigned int lastWrittenBitCount = 0;

// goes low when data is currently being captured
volatile unsigned char flagDone;

// countdown until we assume there are no more bits
volatile unsigned int weigandCounter;

// Display screen timer
unsigned long displayTimeout = 30000;  // 30 seconds
unsigned long lastCardTime = 0;
bool displayingCard = false;

// Wifi Settings
bool APMode;

// AP Settings
// ssid_hidden = broadcast ssid = 0, hidden = 1
// ap_passphrase = NULL for open, min 8 chars, max 63

String ap_ssid;
String ap_passphrase;
int ap_channel;
int ssid_hidden;

// Speaker and LED Settings
int spkOnInvalid;
int spkOnValid;
int ledValid;

// Custom Display Message
String customWelcomeMessage;
String welcomeMessageSelect;

// decoded facility code and card code
unsigned long facilityCode = 0;
unsigned long cardNumber = 0;

// hex data string
String hexCardData;

// raw data string
String rawCardData;
String status;
String details;


// store card data for later review
struct CardData {
  unsigned int bitCount;
  unsigned long facilityCode;
  unsigned long cardNumber;
  String hexCardData;
  String rawCardData;
  String status;   // Add status field
  String details;  // Add details field
};

// breaking up card value into 2 chunks to create 10 char HEX value
volatile unsigned long bitHolder1 = 0;
volatile unsigned long bitHolder2 = 0;
unsigned long cardChunk1 = 0;
unsigned long cardChunk2 = 0;

// Define reader input pins
// card reader DATA0
#define DATA0 19
// card reader DATA1
#define DATA1 18

//define reader output pins
// LED Output for a GND tie back
#define LED 32
// Speaker Output for a GND tie back
#define SPK 33

//define relay modules
#define RELAY1 25
#define RELAY2 26

// store card data for later review
struct Credential {
  unsigned long facilityCode;
  unsigned long cardNumber;
  char name[50]; // Use a fixed-size array for the name
};

const int MAX_CREDENTIALS = 100;
Credential credentials[MAX_CREDENTIALS];
int validCount = 0;

// maximum number of stored cards
const int MAX_CARDS = 100; 
CardData cardDataArray[MAX_CARDS];
int cardDataIndex = 0;

const char *index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Card Data</title>
    <style>
        body {
            font-family: Arial, sans-serif;
        }
        header {
            background-color: #333;
            color: white;
            padding: 10px;
            text-align: center;
        }
        header a {
            color: white;
            margin: 0 15px;
            text-decoration: none;
            cursor: pointer;
        }
        cardTable tbody:nth-child(2) {
           text-decoration: none;
           cursor: pointer;
           target: _blank;
        }
        a:hover {
            text-decoration: underline;
        }
        .content {
            padding: 20px;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-bottom: 20px;
        }
        th, td {
            padding: 8px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background-color: #f2f2f2;
        }
        .hidden {
            display: none;
        }
        .collapsible {
            cursor: pointer;
        }
        .contentCollapsible {
            display: none;
            overflow: hidden;
        }
        .authorized {
            background-color: #90EE90;
        }
        .unauthorized {
            background-color: #FF4433;
        }
        .inputRow td {
            padding: 4px;
        }
        .inputRow input {
            width: 100%;
            box-sizing: border-box;
        }
    </style>
</head>
<body>
    <header>
        <a onclick="showSection('lastRead')">Last Read</a>
        <a onclick="showSection('ctfMode')">CTF Mode</a>
        <a onclick="showSection('settings')">Settings</a>
    </header>
    <div class="content">
        <div id="lastRead">
            <h1>Card Data</h1>
            <table id="cardTable">
                <thead>
                    <tr>
                        <th>#</th>
                        <th>Bit Length</th>
                        <th>Facility Code</th>
                        <th>Card Number</th>
                        <th>Hex Data</th>
                        <th>Raw Data</th>
                    </tr>
                </thead>
                <tbody>
                </tbody>
            </table>
        </div>
        <div id="ctfMode" class="hidden">
            <h1>CTF Mode</h1>
            <h2>Last Read Cards</h2>
            <table id="lastReadCardsTable">
                <thead>
                    <tr>
                        <th>#</th>
                        <th>Status</th>
                        <th>Details</th>
                    </tr>
                </thead>
                <tbody>
                </tbody>
            </table>
            <h2 class="collapsible" onclick="toggleCollapsible()">Card Data (click to expand/collapse)</h2>
            <div class="contentCollapsible">
                <table id="userTable">
                    <thead>
                        <tr>
                            <th>#</th>
                            <th>Facility Code</th>
                            <th>Card Number</th>
                            <th>Name</th>
                            <th>Action</th>
                        </tr>
                    </thead>
                    <tbody>
                    </tbody>
                </table>
                <textarea id="importExportArea" rows="4" cols="50"></textarea>
                <br>
                <button onclick="importData()">Import</button>
                <button onclick="exportData()">Export</button>
            </div>
        </div>
        <div id="settings" class="hidden">
            <h1>Settings</h1>
            <h2>General</h2>
            <label for="modeSelect">Mode:</label>
            <select id="modeSelect">
                <option value="DEMO">Demo</option>
                <option value="CTF">CTF</option>
            </select>
            <br><br>
            <label for="timeoutSelect">Display Timeout:</label>
            <select id="timeoutSelect">
                <option value="20000">20 seconds</option>
                <option value="30000">30 seconds</option>
                <option value="60000">1 minute</option>
                <option value="120000">2 minutes</option>
                <option value="0">Never</option>
            </select>
            <br><br>
            <h3>Wifi</h3>
            <div id="apSettings">
                <label for="ap_ssid">SSID:</label>
                <input type="text" id="ap_ssid">
                <br><br>
                <label for="ap_passphrase">Password:</label>
                <input type="text" id="ap_passphrase">
                <br><br>
                <label for="ssid_hidden">Hidden:</label>
                <input type="checkbox" id="ssid_hidden">
                <br><br>
                <label for="ap_channel">Channel:</label>
                <input type="number" id="ap_channel" min="1" max="12">
            </div>
            <br><br>
            <h2>CTF Mode</h2>
            <label for="welcomeMessageSelect">Welcome Message:</label>
            <select id="welcomeMessageSelect" onchange="toggleWelcomeMessage()">
                <option value="default">Default</option>
                <option value="custom">Custom</option>
            </select>
            <div id="customWelcomeMessage">
                <label for="customMessage">Custom Message:</label>
                <input type="text" id="customMessage" maxlength="20">
            </div>
            <br><br>
            <div>
                <label for="ledValid">On Valid Card - LED:</label>
                <select id="ledValid">
                    <option value="1">Flashing</option>
                    <option value="2">Solid</option>
                    <option value="0">Off</option>
                </select>
                <br><br>
                <label for="spkOnValid">On Valid Card - Speaker:</label>
                <select id="spkOnValid">
                    <option value="1">Pop Beeps</option>
                    <option value="2">Melody Beeps</option>
                    <option value="0">Off</option>
                </select>
            </div>
            <br><br>
            <div>
                <label for="spkOnInvalid">On Invalid Card - Speaker:</label>
                <select id="spkOnInvalid">
                    <option value=1>Sad Beeps</option>
                    <option value=0>Off</option>
                </select>
            </div>
            <br><br>
            <button onclick="saveSettings()">Save Settings</button>
        </div>
    </div>
    <script>
        let cardData = [];
        const tableBody = document.getElementById('cardTable').getElementsByTagName('tbody')[0];
        const userTableBody = document.getElementById('userTable').getElementsByTagName('tbody')[0];
        const lastReadCardsTableBody = document.getElementById('lastReadCardsTable').getElementsByTagName('tbody')[0];
        const importExportArea = document.getElementById('importExportArea');

        function updateTable() {
            fetch('/getCards')
                .then(response => response.json())
                .then(data => {
                    tableBody.innerHTML = '';
                    data.forEach((card, index) => {
                        cardData.push(card);
                        let row = tableBody.insertRow();
                        let cellIndex = row.insertCell(0);
                        let cellBitLength = row.insertCell(1);
                        let cellFacilityCode = row.insertCell(2);
                        let cellCardNumber = row.insertCell(3);
                        let cellHexData = row.insertCell(4);
                        let cellRawData = row.insertCell(5);

                        cellIndex.innerHTML = index + 1;
                        cellBitLength.innerHTML = card.bitCount;
                        cellFacilityCode.innerHTML = card.facilityCode;
                        cellCardNumber.innerHTML = card.cardNumber;
                        cellHexData.innerHTML = `<a href="#" onclick="copyToClipboard('${card.hexCardData}')">${card.hexCardData}</a>`;
                        cellRawData.innerHTML = card.rawCardData;
                    });
                })
                .catch(error => console.error('Error fetching card data:', error));
        }

        function updateUserTable() {
            fetch('/getUsers')
                .then(response => response.json())
                .then(data => {
                    userTableBody.innerHTML = '';
                    data.forEach((user, index) => {
                        let row = userTableBody.insertRow();
                        let cellIndex = row.insertCell(0);
                        let cellFacilityCode = row.insertCell(1);
                        let cellCardNumber = row.insertCell(2);
                        let cellName = row.insertCell(3);
                        let cellAction = row.insertCell(4);

                        cellIndex.innerHTML = index + 1;
                        cellFacilityCode.innerHTML = user.facilityCode;
                        cellCardNumber.innerHTML = user.cardNumber;
                        cellName.innerHTML = user.name;
                        cellAction.innerHTML = '<button onclick="deleteCard(' + index + ')">Delete</button>';
                    });

                    // Add input row at the bottom of the table
                    let inputRow = userTableBody.insertRow();
                    inputRow.className = 'inputRow';
                    let cellIndex = inputRow.insertCell(0);
                    let cellFacilityCode = inputRow.insertCell(1);
                    let cellCardNumber = inputRow.insertCell(2);
                    let cellName = inputRow.insertCell(3);
                    let cellAction = inputRow.insertCell(4);
                    
                    cellFacilityCode.innerHTML = '<input type="number" id="newFacilityCode">';
                    cellCardNumber.innerHTML = '<input type="number" id="newCardNumber">';
                    cellName.innerHTML = '<input type="text" id="newName">';
                    cellAction.innerHTML = '<button onclick="addCard()">Save</button>';
                })
                .catch(error => console.error('Error fetching user data:', error));
        }

        function updateLastReadCardsTable() {
            fetch('/getCards')
                .then(response => response.json())
                .then(data => {
                    lastReadCardsTableBody.innerHTML = '';
                    const last10Cards = data.slice(-10);
                    last10Cards.forEach((card, index) => {
                        let row = lastReadCardsTableBody.insertRow();
                        if (card.status === "Authorized") {
                            row.classList.add("authorized");
                        } else if (card.status === "Unauthorized") {
                            row.classList.add("unauthorized");
                        }
                        let cellIndex = row.insertCell(0);
                        let cellStatus = row.insertCell(1);
                        let cellDetails = row.insertCell(2);

                        cellIndex.innerHTML = index + 1;
                        cellStatus.innerHTML = card.status;
                        cellDetails.innerHTML = card.details;
                    });

                    // Add empty rows if there are less than 10 entries
                    for (let i = last10Cards.length; i < 10; i++) {
                        let row = lastReadCardsTableBody.insertRow();
                        let cellIndex = row.insertCell(0);
                        let cellStatus = row.insertCell(1);
                        let cellDetails = row.insertCell(2);

                        cellIndex.innerHTML = i + 1;
                        cellStatus.innerHTML = "";
                        cellDetails.innerHTML = "";
                    }
                })
                .catch(error => console.error('Error fetching last read card data:', error));
        }

        function addCard() {
            const facilityCode = document.getElementById('newFacilityCode').value;
            const cardNumber = document.getElementById('newCardNumber').value;
            const name = document.getElementById('newName').value;

            fetch(`/addCard?facilityCode=${facilityCode}&cardNumber=${cardNumber}&name=${name}`)
                .then(response => {
                    if (response.ok) {
                        updateUserTable();
                        alert('Card added successfully');
                    } else {
                        alert('Failed to add card');
                    }
                })
                .catch(error => console.error('Error adding card:', error));
        }

        function deleteCard(index) {
            fetch(`/deleteCard?index=${index}`)
                .then(response => {
                    if (response.ok) {
                        updateUserTable();
                        alert('Card deleted successfully');
                    } else {
                        alert('Failed to delete card');
                    }
                })
                .catch(error => console.error('Error deleting card:', error));
        }

        function showSection(section) {
            document.getElementById('lastRead').classList.add('hidden');
            document.getElementById('ctfMode').classList.add('hidden');
            document.getElementById('settings').classList.add('hidden');
            document.getElementById(section).classList.remove('hidden');
        }

        function toggleCollapsible() {
            const content = document.querySelector(".contentCollapsible");
            content.style.display = content.style.display === "block" ? "none" : "block";
        }

        function toggleWelcomeMessage() {
            const welcomeMessageSelect = document.getElementById('welcomeMessageSelect').value;
            const customMessageInput = document.getElementById('customMessage');
            if (welcomeMessageSelect === 'custom') {
                customMessageInput.disabled = false;
            } else {
                customMessageInput.disabled = true;
            }
        }

        function updateSettingsUI(settings) {
            document.getElementById('modeSelect').value = settings.mode;
            document.getElementById('timeoutSelect').value = settings.displayTimeout;
            document.getElementById('ap_ssid').value = settings.apSsid;
            document.getElementById('ap_passphrase').value = settings.apPassphrase;
            document.getElementById('ssid_hidden').checked = settings.ssidHidden;
            document.getElementById('ap_channel').value = settings.apChannel;
            document.getElementById('welcomeMessageSelect').value = settings.welcomeMessageSelect;
            document.getElementById('customMessage').value = settings.customMessage;
            document.getElementById('ledValid').value = settings.ledValid;
            document.getElementById('spkOnValid').value = settings.spkOnValid;
            document.getElementById('spkOnInvalid').value = settings.spkOnInvalid;
            toggleWifiSettings();
            toggleWelcomeMessage();
        }

        function saveSettings() {
            const mode = document.getElementById('modeSelect').value;
            const timeout = document.getElementById('timeoutSelect').value;
            const apSsid = document.getElementById('ap_ssid').value;
            const apPassphrase = document.getElementById('ap_passphrase').value;
            const ssidHidden = document.getElementById('ssid_hidden').checked;
            const apChannel = document.getElementById('ap_channel').value;
            const welcomeMessageSelect = document.getElementById('welcomeMessageSelect').value;
            const customMessage = document.getElementById('customMessage').value;
            const ledValid = document.getElementById('ledValid').value;
            const spkOnValid = document.getElementById('spkOnValid').value;
            const spkOnInvalid = document.getElementById('spkOnInvalid').value;

            let settings = {
                mode: mode,
                displayTimeout: parseInt(timeout, 10),
                apSsid: apSsid,
                apPassphrase: apPassphrase,
                ssidHidden: ssidHidden,
                apChannel: parseInt(apChannel),
                welcomeMessageSelect: welcomeMessageSelect,
                customMessage: customMessage,
                ledValid: parseInt(ledValid),
                spkOnValid: parseInt(spkOnValid),
                spkOnInvalid: parseInt(spkOnInvalid)
            };

            fetch('/saveSettings', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(settings)
            })
            .then(response => {
                if (response.ok) {
                    alert('Settings saved successfully');
                } else {
                    alert('Failed to save settings');
                }
            })
            .catch(error => console.error('Error saving settings:', error));
        }

        function fetchSettings() {
            fetch('/getSettings')
                .then(response => response.json())
                .then(data => {
                    updateSettingsUI(data);
                })
                .catch(error => console.error('Error fetching settings:', error));
        }

        window.onload = fetchSettings;
        function exportData() {
            fetch('/getUsers')
                .then(response => response.json())
                .then(data => {
                    const dataString = JSON.stringify(data);
                    importExportArea.value = dataString;
                    importExportArea.select();
                    document.execCommand('copy');
                    alert('Data exported to clipboard');
                })
                .catch(error => console.error('Error exporting data:', error));
        }

        function importData() {
            const dataString = importExportArea.value;
            try {
                const data = JSON.parse(dataString);
                if (Array.isArray(data)) {
                    data.forEach(card => {
                        const facilityCode = card.facilityCode;
                        const cardNumber = card.cardNumber;
                        const name = card.name;

                        fetch(`/addCard?facilityCode=${facilityCode}&cardNumber=${cardNumber}&name=${name}`)
                            .then(response => {
                                if (!response.ok) {
                                    throw new Error('Failed to add card');
                                }
                            })
                            .catch(error => console.error('Error adding card:', error));
                    });
                    updateUserTable();
                    alert('Data imported successfully');
                } else {
                    alert('Invalid data format');
                }
            } catch (error) {
                alert('Invalid JSON format');
            }
        }

        function copyToClipboard(text) {
            const tempInput = document.createElement('input');
            tempInput.style.position = 'absolute';
            tempInput.style.left = '-9999px';
            tempInput.value = text;
            document.body.appendChild(tempInput);
            tempInput.select();
            document.execCommand('copy');
            document.body.removeChild(tempInput);
        }

        setInterval(updateTable, 5000);
        setInterval(updateLastReadCardsTable, 5000);
        updateTable();
        updateUserTable();
        updateLastReadCardsTable();
    </script>
</body>
</html>
)rawliteral";

// Interrupts for card reader
void ISR_INT0() {
  bitCount++;
  flagDone = 0;

  if (bitCount < 23) {
    bitHolder1 = bitHolder1 << 1;
  } else {
    bitHolder2 = bitHolder2 << 1;
  }
  weigandCounter = WEIGAND_WAIT_TIME;
}

// interrupt that happens when INT1 goes low (1 bit)
void ISR_INT1() {
  if (bitCount < MAX_BITS) {
    databits[bitCount] = 1;
    bitCount++;
  }
  flagDone = 0;

  if (bitCount < 23) {
    bitHolder1 = bitHolder1 << 1;
    bitHolder1 |= 1;
  } else {
    bitHolder2 = bitHolder2 << 1;
    bitHolder2 |= 1;
  }

  weigandCounter = WEIGAND_WAIT_TIME;
}

void saveSettingsToPreferences() {
  preferences.begin("settings", false);
  preferences.putString("MODE", MODE);
  preferences.putULong("displayTimeout", displayTimeout);
  preferences.putBool("APMode", APMode);
  preferences.putString("ap_ssid", ap_ssid);
  preferences.putString("ap_passphrase", ap_passphrase);
  preferences.putInt("ap_channel", ap_channel);
  preferences.putInt("ssid_hidden", ssid_hidden);
  preferences.putInt("spkOnInvalid", spkOnInvalid);
  preferences.putInt("spkOnValid", spkOnValid);
  preferences.putInt("ledValid", ledValid);
  preferences.putString("customWelcomeMessage", customWelcomeMessage);
  preferences.putString("welcomeMessageSelect", welcomeMessageSelect);
  preferences.end();
}

void loadSettingsFromPreferences() {
  preferences.begin("settings", false);
  MODE = preferences.getString("MODE", "CTF");
  displayTimeout = preferences.getULong("displayTimeout", 30000);
  APMode = preferences.getBool("APMode", true);
  ap_ssid = preferences.getString("ap_ssid","doorsim");
  ap_passphrase = preferences.getString("ap_passphrase", "");
  ap_channel = preferences.getInt("ap_channel",1);
  ssid_hidden = preferences.getInt("ssid_hidden",0);
  spkOnInvalid = preferences.getInt("spkOnInvalid",1);
  spkOnValid = preferences.getInt("spkOnValid",1);
  ledValid = preferences.getInt("ledValid",1);
  customWelcomeMessage = preferences.getString("customWelcomeMessage","");
  welcomeMessageSelect = preferences.getString("welcomeMessageSelect","default");
  preferences.end();
}

void saveCredentialsToPreferences() {
  preferences.begin("credentials", false);
  preferences.putInt("validCount", validCount);
  for (int i = 0; i < validCount; i++) {
    String fcKey = "fc" + String(i);
    String cnKey = "cn" + String(i);
    String nameKey = "name" + String(i);
    preferences.putUInt(fcKey.c_str(), credentials[i].facilityCode);
    preferences.putUInt(cnKey.c_str(), credentials[i].cardNumber);
    preferences.putString(nameKey.c_str(), credentials[i].name);
  }
  preferences.end();
  Serial.println("Credentials saved to Preferences:");
  for (int i = 0; i < validCount; i++) {
    Serial.print("Credential ");
    Serial.print(i);
    Serial.print(": FC=");
    Serial.print(credentials[i].facilityCode);
    Serial.print(", CN=");
    Serial.print(credentials[i].cardNumber);
    Serial.print(", Name=");
    Serial.println(credentials[i].name);
  }
  Serial.print("Valid Count: ");
  Serial.println(validCount);
}

void loadCredentialsFromPreferences() {
  preferences.begin("credentials", true);
  validCount = preferences.getInt("validCount", 0);
  for (int i = 0; i < validCount; i++) {
    String fcKey = "fc" + String(i);
    String cnKey = "cn" + String(i);
    String nameKey = "name" + String(i);
    credentials[i].facilityCode = preferences.getUInt(fcKey.c_str(), 0);
    credentials[i].cardNumber = preferences.getUInt(cnKey.c_str(), 0);
    String name = preferences.getString(nameKey.c_str(), "");
    strncpy(credentials[i].name, name.c_str(), sizeof(credentials[i].name) - 1);
    credentials[i].name[sizeof(credentials[i].name) - 1] = '\0';
  }
  preferences.end();
  Serial.println("Credentials loaded from Preferences:");
  for (int i = 0; i < validCount; i++) {
    Serial.print("Credential ");
    Serial.print(i);
    Serial.print(": FC=");
    Serial.print(credentials[i].facilityCode);
    Serial.print(", CN=");
    Serial.print(credentials[i].cardNumber);
    Serial.print(", Name=");
    Serial.println(credentials[i].name);
  }
  Serial.print("Valid Count: ");
  Serial.println(validCount);
}

// Check if credential is valid
const Credential *checkCredential(uint16_t fc, uint16_t cn) {
  for (unsigned int i = 0; i < validCount; i++) {
    if (credentials[i].facilityCode == fc && credentials[i].cardNumber == cn) {
      // Found a matching credential, return a pointer to it
      return &credentials[i];
    }
  }
  // No matching credential found, return nullptr
  return nullptr;
}

void printCardData() {
  if (MODE == "CTF") {
    const Credential* result = checkCredential(facilityCode, cardNumber);
    if (result != nullptr) {
      // Valid credential found
      Serial.println("Valid credential found:");
      Serial.println("FC: " + String(result->facilityCode) + ", CN: " + String(result->cardNumber) + ", Name: " + result->name);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Card Read: ");
      lcd.setCursor(11, 0);
      lcd.print("VALID");
      lcd.setCursor(0, 1);
      lcd.print("FC: " + String(result->facilityCode));
      lcd.setCursor(9, 1);
      lcd.print("CN:" + String(result->cardNumber));
      lcd.setCursor(0, 3);
      lcd.print("Name: " + String(result->name));
      ledOnValid();
      speakerOnValid();

      // Update card data status and details
      status = "Authorized";
      details = result->name;
    } else {
      // No valid credential found
      Serial.println("Error: No valid credential found.");
      lcdInvalidCredentials();
      speakerOnFailure();

      // Update card data status and details
      status = "Unauthorized";
      details = "FC: " + String(facilityCode) + ", CN: " + String(cardNumber);
    }
  } else {
    // ranges for "valid" bitCount are a bit larger for debugging
    if (bitCount > 20 && bitCount < 120) {
      // ignore data caused by noise
      Serial.print("[*] Bit length: ");
      Serial.println(bitCount);
      Serial.print("[*] Facility code: ");
      Serial.println(facilityCode);
      Serial.print("[*] Card number: ");
      Serial.println(cardNumber);
      Serial.print("[*] Hex: ");
      Serial.println(hexCardData);
      Serial.print("[*] Raw: ");
      Serial.println(rawCardData);

      // LCD Printing
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Card Read: ");
      lcd.setCursor(11, 0);
      lcd.print(bitCount);
      lcd.print("bits");
      lcd.setCursor(0, 1);
      lcd.print("FC: ");
      lcd.print(facilityCode);
      lcd.setCursor(9, 1);
      lcd.print(" CN: ");
      lcd.print(cardNumber);
      lcd.setCursor(0, 3);
      lcd.print("Hex: ");
      hexCardData.toUpperCase();
      lcd.print(hexCardData);

      // Update card data status and details
      status = "Read";
      details = "Hex: " + hexCardData;
    }
  }

  // Store card data
  if (cardDataIndex < MAX_CARDS) {
    cardDataArray[cardDataIndex].bitCount = bitCount;
    cardDataArray[cardDataIndex].facilityCode = facilityCode;
    cardDataArray[cardDataIndex].cardNumber = cardNumber;
    cardDataArray[cardDataIndex].hexCardData = hexCardData;
    cardDataArray[cardDataIndex].rawCardData = rawCardData;
    cardDataArray[cardDataIndex].status = status;
    cardDataArray[cardDataIndex].details = details;
    cardDataIndex++;
  }

  // Start the display timer
  lastCardTime = millis();
  displayingCard = true;
}

// Functions to handle invalid credentials
void lcdInvalidCredentials() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Card Read: ");
  lcd.setCursor(11, 0);
  lcd.print("INVALID");
  lcd.setCursor(0, 2);
  lcd.print(" THIS INCIDENT WILL");
  lcd.setCursor(0, 3);
  lcd.print("    BE REPORTED    ");
}


void speakerOnFailure() {
  switch (spkOnInvalid) {
    case 0:
      break;
    
    case 1:
      // Sad Beeps
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      delay(50);
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      delay(50);
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      delay(50);
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      break;

  }
}

// Functions to handle valid credentials
void speakerOnValid() {
  switch (spkOnValid) {
    case 0 :
      break;
    
    case 1:
      // Nice Beeps LED
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      delay(50);
      digitalWrite(SPK, LOW);
      delay(100);
      digitalWrite(SPK, HIGH);
      break;

    case 2:
      // Long Beeps
      digitalWrite(SPK, LOW);
      delay(2000);
      digitalWrite(SPK, HIGH);
      break;
    }
}

void ledOnValid() {
  switch (ledValid) {
    case 0:
      break;
    
    case 1:
      // Flashing LED
      digitalWrite(LED, LOW);
      delay(250);
      digitalWrite(LED, HIGH);
      delay(100);
      digitalWrite(LED, LOW);
      delay(250);
      digitalWrite(LED, HIGH);
      break;

    
    case 2:
      digitalWrite(LED, LOW);
      delay(2000);
      digitalWrite(LED, HIGH);
      break;

  }
}

// Process hid cards
unsigned long decodeHIDFacilityCode(unsigned int start, unsigned int end) {
  unsigned long HIDFacilityCode = 0;
  for (unsigned int i = start; i < end; i++) {
    HIDFacilityCode = (HIDFacilityCode << 1) | databits[i];
  }
  return HIDFacilityCode;
}

unsigned long decodeHIDCardNumber(unsigned int start, unsigned int end) {
  unsigned long HIDCardNumber = 0;
  for (unsigned int i = start; i < end; i++) {
    HIDCardNumber = (HIDCardNumber << 1) | databits[i];
  }
  return HIDCardNumber;
}

// Card value processing functions
// Function to append the card value (bitHolder1 and bitHolder2) to the
// necessary array then translate that to the two chunks for the card value that
// will be output
void setCardChunkBits(unsigned int cardChunk1Offset, unsigned int bitHolderOffset, unsigned int cardChunk2Offset) {
  for (int i = 19; i >= 0; i--) {
    if (i == 13 || i == cardChunk1Offset) {
      bitWrite(cardChunk1, i, 1);
    } else if (i > cardChunk1Offset) {
      bitWrite(cardChunk1, i, 0);
    } else {
      bitWrite(cardChunk1, i, bitRead(bitHolder1, i + bitHolderOffset));
    }
    if (i < bitHolderOffset) {
      bitWrite(cardChunk2, i + cardChunk2Offset, bitRead(bitHolder1, i));
    }
    if (i < cardChunk2Offset) {
      bitWrite(cardChunk2, i, bitRead(bitHolder2, i));
    }
  }
}

String prefixPad(const String &in, const char c, const size_t len) {
  String out = in;
  while (out.length() < len) {
    out = c + out;
  }
  return out;
}

void processHIDCard() {
  // bits to be decoded differently depending on card format length
  // see http://www.pagemac.com/projects/rfid/hid_data_formats for more info
  // also specifically: www.brivo.com/app/static_data/js/calculate.js
  // Example of full card value
  // |>   preamble   <| |>   Actual card value   <|
  // 000000100000000001 11 111000100000100100111000
  // |> write to chunk1 <| |>  write to chunk2   <|

  unsigned int cardChunk1Offset, bitHolderOffset, cardChunk2Offset;

  switch (bitCount) {
    case 26:
      facilityCode = decodeHIDFacilityCode(1, 9);
      cardNumber = decodeHIDCardNumber(9, 25);
      cardChunk1Offset = 2;
      bitHolderOffset = 20;
      cardChunk2Offset = 4;
      break;

    case 27:
      facilityCode = decodeHIDFacilityCode(1, 13);
      cardNumber = decodeHIDCardNumber(13, 27);
      cardChunk1Offset = 3;
      bitHolderOffset = 19;
      cardChunk2Offset = 5;
      break;

    case 29:
      facilityCode = decodeHIDFacilityCode(1, 13);
      cardNumber = decodeHIDCardNumber(13, 29);
      cardChunk1Offset = 5;
      bitHolderOffset = 17;
      cardChunk2Offset = 7;
      break;

    case 30:
      facilityCode = decodeHIDFacilityCode(1, 13);
      cardNumber = decodeHIDCardNumber(13, 29);
      cardChunk1Offset = 6;
      bitHolderOffset = 16;
      cardChunk2Offset = 8;
      break;

    case 31:
      facilityCode = decodeHIDFacilityCode(1, 5);
      cardNumber = decodeHIDCardNumber(5, 28);
      cardChunk1Offset = 7;
      bitHolderOffset = 15;
      cardChunk2Offset = 9;
      break;

    // modified to wiegand 32 bit format instead of HID
    case 32:
      facilityCode = decodeHIDFacilityCode(5, 16);
      cardNumber = decodeHIDCardNumber(17, 32);
      cardChunk1Offset = 8;
      bitHolderOffset = 14;
      cardChunk2Offset = 10;
      break;

    case 33:
      facilityCode = decodeHIDFacilityCode(1, 8);
      cardNumber = decodeHIDCardNumber(8, 32);
      cardChunk1Offset = 9;
      bitHolderOffset = 13;
      cardChunk2Offset = 11;
      break;

    case 34:
      facilityCode = decodeHIDFacilityCode(1, 17);
      cardNumber = decodeHIDCardNumber(17, 33);
      cardChunk1Offset = 10;
      bitHolderOffset = 12;
      cardChunk2Offset = 12;
      break;

    case 35:
      facilityCode = decodeHIDFacilityCode(2, 14);
      cardNumber = decodeHIDCardNumber(14, 34);
      cardChunk1Offset = 11;
      bitHolderOffset = 11;
      cardChunk2Offset = 13;
      break;

    case 36:
      facilityCode = decodeHIDFacilityCode(21, 33);
      cardNumber = decodeHIDCardNumber(1, 17);
      cardChunk1Offset = 12;
      bitHolderOffset = 10;
      cardChunk2Offset = 14;
      break;

    default:
      Serial.println("[-] Unsupported bitCount for HID card");
      return;
  }

  setCardChunkBits(cardChunk1Offset, bitHolderOffset, cardChunk2Offset);
  hexCardData = String(cardChunk1, HEX) + prefixPad(String(cardChunk2, HEX), '0', 6);
  //hexCardData = String(cardChunk1, HEX) + String(cardChunk2, HEX);
}

void processCardData() {
  rawCardData = "";
  for (unsigned int i = 0; i < bitCount; i++) {
    rawCardData += String(databits[i]);
  }

  if (bitCount >= 26 && bitCount <= 96) {
    processHIDCard();
  }
}

void clearDatabits() {
  for (unsigned char i = 0; i < MAX_BITS; i++) {
    databits[i] = 0;
  }
}

// reset variables and prepare for the next card read
void cleanupCardData() {
  rawCardData = "";
  hexCardData = "";
  bitCount = 0;
  facilityCode = 0;
  cardNumber = 0;
  bitHolder1 = 0;
  bitHolder2 = 0;
  cardChunk1 = 0;
  cardChunk2 = 0;
  status = "";
  details = "";

}

bool allBitsAreOnes() {
  for (int i = 0; i < MAX_BITS; i++) {
    if (databits[i] != 0xFF) {  // Check if each byte is not equal to 0xFF
      return false;             // If any byte is not 0xFF, not all bits are ones
    }
  }
  return true;  // All bytes were 0xFF, so all bits are ones
}

String centerText(const String &text, int width) {
  int len = text.length();
  if (len >= width) {
    return text;
  }
  int padding = (width - len) / 2;
  String spaces = "";
  for (int i = 0; i < padding; i++) {
    spaces += " ";
  }
  return spaces + text;
}

void printWelcomeMessage() {
  if (MODE == "CTF") {
    if (customWelcomeMessage != NULL) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(centerText(String(customWelcomeMessage), 20));
      lcd.setCursor(0, 2);
      lcd.print(centerText("Present Card", 20));
    }
    else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(centerText("CTF Mode", 20));
      lcd.setCursor(0, 2);
      lcd.print(centerText("Present Card", 20));
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("Door Sim - Ready", 20));
    lcd.setCursor(0, 2);
    lcd.print(centerText("Present Card", 20));  
  }
}

void updateDisplay() {
  if (displayingCard && (millis() - lastCardTime >= displayTimeout)) {
    printWelcomeMessage();
    displayingCard = false;
  }
}

void printAllCardData() {
  Serial.println("Previously read card data:");
  for (int i = 0; i < cardDataIndex; i++) {
    Serial.print(i + 1);
    Serial.print(": Bit length: ");
    Serial.print(cardDataArray[i].bitCount);
    Serial.print(", Facility code: ");
    Serial.print(cardDataArray[i].facilityCode);
    Serial.print(", Card number: ");
    Serial.print(cardDataArray[i].cardNumber);
    Serial.print(", Hex: ");
    Serial.print(cardDataArray[i].hexCardData);
    Serial.print(", Raw: ");
    Serial.println(cardDataArray[i].rawCardData);
  }
}

void setupWifi() {
    WiFi.softAP(ap_ssid, ap_passphrase, ap_channel, ssid_hidden);
}

void setup() {
  Serial.begin(115200);
  lcd.begin();

  pinMode(DATA0, INPUT);
  pinMode(DATA1, INPUT);

  pinMode(LED, OUTPUT);
  pinMode(SPK, OUTPUT);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);

  digitalWrite(LED, HIGH);
  digitalWrite(SPK, HIGH);

  attachInterrupt(DATA0, ISR_INT0, FALLING);
  attachInterrupt(DATA1, ISR_INT1, FALLING);

  weigandCounter = WEIGAND_WAIT_TIME;
  for (unsigned char i = 0; i < MAX_BITS; i++) {
    lastWrittenDatabits[i] = 0;
  }

  loadSettingsFromPreferences();
  loadCredentialsFromPreferences();

  setupWifi();

  printWelcomeMessage();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/getCards", HTTP_GET, [](AsyncWebServerRequest *request) {
      DynamicJsonDocument doc(4096);
      JsonArray cards = doc.to<JsonArray>();
      for (int i = 0; i < cardDataIndex; i++) {
          JsonObject card = cards.createNestedObject();
          card["bitCount"] = cardDataArray[i].bitCount;
          card["facilityCode"] = cardDataArray[i].facilityCode;
          card["cardNumber"] = cardDataArray[i].cardNumber;
          card["hexCardData"] = cardDataArray[i].hexCardData;
          card["rawCardData"] = cardDataArray[i].rawCardData;
          card["status"] = cardDataArray[i].status;
          card["details"] = cardDataArray[i].details;
      }
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
  });

  server.on("/getUsers", HTTP_GET, [](AsyncWebServerRequest *request) {
      DynamicJsonDocument doc(4096);
      JsonArray users = doc.to<JsonArray>();
      for (int i = 0; i < validCount; i++) {
          JsonObject user = users.createNestedObject();
          user["facilityCode"] = credentials[i].facilityCode;
          user["cardNumber"] = credentials[i].cardNumber;
          user["name"] = credentials[i].name;
      }
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
  });

  server.on("/getSettings", HTTP_GET, [](AsyncWebServerRequest *request) {
      DynamicJsonDocument doc(2048);
      doc["mode"] = MODE;
      doc["displayTimeout"] = displayTimeout;
      doc["apSsid"] = ap_ssid;
      doc["apPassphrase"] = ap_passphrase;
      doc["ssidHidden"] = ssid_hidden;
      doc["apChannel"] = ap_channel;
      doc["welcomeMessageSelect"] = customWelcomeMessage.length() > 0 ? "custom" : "default";
      doc["customMessage"] = customWelcomeMessage;
      doc["ledValid"] = ledValid;
      doc["spkOnValid"] = spkOnValid;
      doc["spkOnInvalid"] = spkOnInvalid;
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
  });

  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/saveSettings", [](AsyncWebServerRequest *request, JsonVariant &json) {
      JsonObject jsonObj = json.as<JsonObject>();

      // Parse the JSON and update settings
      MODE = jsonObj["mode"] | "CTF";
      displayTimeout = jsonObj["displayTimeout"] | 30000;
      ap_ssid = jsonObj["apSsid"] | "doorsim";
      ap_passphrase = jsonObj["apPassphrase"] | "";
      ap_channel = jsonObj["apChannel"] | 1;
      ssid_hidden = jsonObj["ssidHidden"] | 0;
      welcomeMessageSelect = jsonObj["welcomeMessageSelect"] | "default";
      customWelcomeMessage = jsonObj["customMessage"] | "";
      spkOnInvalid = jsonObj["spkOnInvalid"] | 1;
      spkOnValid = jsonObj["spkOnValid"] | 1;
      ledValid = jsonObj["ledValid"] | 1;

      saveSettingsToPreferences();
      setupWifi();

      request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  server.addHandler(handler);

  server.on("/addCard", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (validCount < MAX_CREDENTIALS) {
      if (request->hasParam("facilityCode") && request->hasParam("cardNumber") && request->hasParam("name")) {
        String facilityCodeStr = request->getParam("facilityCode")->value();
        String cardNumberStr = request->getParam("cardNumber")->value();
        String name = request->getParam("name")->value();

        credentials[validCount].facilityCode = facilityCodeStr.toInt();
        credentials[validCount].cardNumber = cardNumberStr.toInt();
        strncpy(credentials[validCount].name, name.c_str(), sizeof(credentials[validCount].name) - 1);
        credentials[validCount].name[sizeof(credentials[validCount].name) - 1] = '\0';
        validCount++;
        saveCredentialsToPreferences();
        request->send(200, "text/plain", "Card added successfully");
      } else {
        request->send(400, "text/plain", "Missing parameters");
      }
    } else {
      request->send(500, "text/plain", "Max number of credentials reached");
    }
  });

  server.on("/deleteCard", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("index")) {
      int index = request->getParam("index")->value().toInt();
      if (index >= 0 && index < validCount) {
        for (int i = index; i < validCount - 1; i++) {
          credentials[i] = credentials[i + 1];
        }
        validCount--;
        saveCredentialsToPreferences();
        request->send(200, "text/plain", "Card deleted successfully");
      } else {
        request->send(400, "text/plain", "Invalid index");
      }
    } else {
      request->send(400, "text/plain", "Missing index parameter");
    }
  });

  server.on("/exportData", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray users = doc.createNestedArray("users");
    for (int i = 0; i < validCount; i++) {
        JsonObject user = users.createNestedObject();
        user["facilityCode"] = credentials[i].facilityCode;
        user["cardNumber"] = credentials[i].cardNumber;
        user["name"] = credentials[i].name;
    }
    JsonArray cards = doc.createNestedArray("cards");
    for (int i = 0; i < cardDataIndex; i++) {
        JsonObject card = cards.createNestedObject();
        card["bitCount"] = cardDataArray[i].bitCount;
        card["facilityCode"] = cardDataArray[i].facilityCode;
        card["cardNumber"] = cardDataArray[i].cardNumber;
        card["hexCardData"] = cardDataArray[i].hexCardData;
        card["rawCardData"] = cardDataArray[i].rawCardData;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.begin();

  digitalWrite(RELAY1, HIGH);

}

void loop() {
  updateDisplay();

  if (!flagDone) {
    if (--weigandCounter == 0)
      flagDone = 1;
  }

  if (bitCount > 0 && flagDone) {
    if (!allBitsAreOnes()) {
      processCardData();
      if (bitCount >= 26 && bitCount <= 36 || bitCount == 96) {
        printCardData();
        printAllCardData();
      }
    }


    cleanupCardData();
    clearDatabits();
  }
}
