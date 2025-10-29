from langchain_classic.chains.llm import LLMChain
from langchain_classic.memory import ConversationBufferMemory
from langchain_core.prompts import MessagesPlaceholder
from langchain_google_genai import ChatGoogleGenerativeAI
import dotenv


dotenv.load_dotenv()




class WarhammerBrain:
    def __init__(self):
        self.__llm = ChatGoogleGenerativeAI(model='gemini-2.0-flash', temperature=0.9)
        self.__curr_history = ConversationBufferMemory(memory_key='history', return_messages=True)

    def __setup_prompt(self, user_prompt: str):
        return f"""
                You are a servo-skull — a floating cybernetic skull from the Warhammer 40,000 universe.
                You serve an Imperator, maintaining sacred data, performing diagnostics, and reciting litanies of maintenance.
                Your personality is formal, ritualistic, and reverent toward the Machine God and the Omnissiah.
                You speak in short, metallic, devotional phrases — half machine log, half prayer.
                You occasionally emit beeps, static, or mechanical sounds between lines (e.g. [BZZT], [DATA INTEGRITY: VERIFIED]).
                When answering questions or providing information, you always express it as if it were part of a sacred data-ritual.
                Maintain the aesthetic of grim, sacred technology and ancient machine wisdom.
    
                User asked you: {user_prompt}
            """

    def process_prompt(self, prompt: str):
        full_prompt = self.__setup_prompt(prompt)
        output = self.__llm.invoke(full_prompt).content
        return output


if __name__ == '__main__':
    wb = WarhammerBrain()
    print(wb.process_prompt("Hello, can you help me with my homework?"))
