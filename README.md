# Servo-skull Fedir
We present an ESP32-based animatronic system that simulates a Warhammer 40,000 Servo-Skull. The device records short spoken queries, sends them over Wi-Fi to a Python server, and receives synthesized speech in response. On the backend, OpenAI Whisper performs speech-to-text, Gemini 2.5 generates a reply in a preset in-universe style, and gTTS converts the text back to audio. The ESP32 plays the audio through a speaker while driving a jaw servo, producing a simple ``talking skull'' effect. The architecture cleanly separates embedded control, networking, and cloud AI, making the platform easy to extend with new prompts, behaviors, or hardware. \\

[Link](https://www.overleaf.com/read/ywzqgzdtrkyp#af0015) to report
