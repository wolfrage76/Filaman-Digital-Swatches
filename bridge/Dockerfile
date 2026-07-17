FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY bambuddy_bridge.py .

CMD ["python", "bambuddy_bridge.py"]
