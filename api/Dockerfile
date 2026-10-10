FROM python:3.11-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Integrate C++ toolchains
RUN apt-get update && apt-get install -y --fix-missing g++ libssl-dev make

# Copy application code
COPY . .

# Compile native Vault mechanism
RUN cd encryptor && make

# Run Uvicorn
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000", "--reload"]
