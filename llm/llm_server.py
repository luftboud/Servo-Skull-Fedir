import time

import dotenv
import pydantic
from gtts import gTTS
from fastapi import FastAPI, Response
from fastapi.responses import FileResponse
from pydub import AudioSegment
from groq import Groq
import os
import base64

from llm import WarhammerBrain


class LLMInputModel(pydantic.BaseModel):
    user_prompt: str


class UploadOutput(pydantic.BaseModel):
    filename: str


app = FastAPI()
brain_llm = WarhammerBrain()


UPLOAD_DIR = "uploads"
os.makedirs(UPLOAD_DIR, exist_ok=True)
dotenv.load_dotenv()


@app.get("/audio/{filename}")
async def audio(filename: str):
    file_path = os.path.join(UPLOAD_DIR, filename)
    return FileResponse(file_path, media_type="audio/mpeg")


@app.post('/ask_llm_mp3')
def ask_llm_mp3(request: LLMInputModel):
    decoded_bytes = base64.b64decode(request.user_prompt)
    input_filename = 'input.wav'
    with open(os.path.join(UPLOAD_DIR, input_filename), "wb") as f:
        f.write(decoded_bytes)

    model = Groq(api_key=os.getenv("GROQ_API_KEY"))

    with open(os.path.join(UPLOAD_DIR, input_filename), "rb") as f:
        result = model.audio.transcriptions.create(
            file=f,
            model="whisper-large-v3",
            response_format="verbose_json",
            language="en"
        )

    os.remove(os.path.join(UPLOAD_DIR, input_filename))

    response = brain_llm.process_prompt(result.text)
    print(response)
    tts = gTTS(response)
    file_path = os.path.join(UPLOAD_DIR, "sound.mp3")
    tts.save(file_path)

    time.sleep(0.5)

    a_s = AudioSegment.from_file(file_path, format='mp3')
    a_s.set_channels(1)
    a_s.set_frame_rate(16000)

    wav_file_path = os.path.join(UPLOAD_DIR, "sound.wav")
    a_s.export(wav_file_path, format="wav")


@app.post('/ask_llm')
async def ask_llm(request: LLMInputModel):
    response = brain_llm.process_prompt(request.user_prompt)
    return Response(
        response,
        media_type="text/plain",
    )


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
