import pydantic
from fastapi import FastAPI

from llm import WarhammerBrain


class LLMInputModel(pydantic.BaseModel):
    user_prompt: str


class LLMOutputModel(pydantic.BaseModel):
    response: str

app = FastAPI()
brain_llm = WarhammerBrain()


@app.post('/ask_llm')
async def ask_llm(request: LLMInputModel):
    response = brain_llm.process_prompt(request.user_prompt)
    output = LLMOutputModel(response=response)
    return output


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
