import pydantic
from gtts import gTTS
from fastapi import FastAPI
import uuid

from llm import WarhammerBrain


class LLMInputModel(pydantic.BaseModel):
    user_prompt: str


app = FastAPI()
brain_llm = WarhammerBrain()


@app.post('/ask_llm_mp3')
def ask_llm_mp3(request: LLMInputModel):
    response = brain_llm.process_prompt(request.user_prompt)
    tts = gTTS(response)
    name = str(uuid.uuid4())[:8]
    filename = f'../llm_launcher/mp3_output/{name}.mp3'
    tts.save(filename)

    return filename

@app.post('/ask_llm')
async def ask_llm(request: LLMInputModel):
    response = brain_llm.process_prompt(request.user_prompt)
    return response


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
