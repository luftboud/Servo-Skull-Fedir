#include <WiFi.h>
#include <HTTPClient.h>


const char* ssid = "UCU_Guest";
// const char* pwd = "tQI4451BB";

void setup() {
    Serial.begin(9600);
    WiFi.begin(ssid);

    Serial.println("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.println("Connected to WiFi");
}

void send_prompt(const String& prompt) {
    Serial.println("Connecting to the API server");

    HTTPClient client;
    client.begin("http://10.10.245.135:8000/ask_llm");
    client.setTimeout(20000);

    String raw_prompt = prompt;
    raw_prompt.replace("\"", "\\\"");

    String json = "{\"user_prompt\": \"" + raw_prompt + "\"}";

    int code = client.POST(json);
    if (code > 0) {
        if (code == 200) {
            String resp = client.getString();
            Serial.println("Response: " + resp);
        } else {
            Serial.println("Something went wrong");
        }
    } else {
        Serial.println("POST failed, error: " + String(client.errorToString(code).c_str()));
    }

    client.end();
}

void loop() {
    String prompt = "Hello. Can you help me with my homework?";
    send_prompt(prompt);
}