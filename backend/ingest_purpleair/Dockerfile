FROM python:3.12-slim

WORKDIR /app

# Install uv for fast dependency management
RUN pip install --no-cache-dir uv

# Copy project files
COPY pyproject.toml .
COPY src/ src/

# Install dependencies using uv
RUN uv pip install --system --no-cache .

# Run the service
CMD ["python", "-m", "ingest_purpleair.main"]
