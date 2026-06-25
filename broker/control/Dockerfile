FROM python:3.12-slim
# tzdata: zoneinfo needs the tz database (slim has no system copy)
RUN pip install --no-cache-dir aiomqtt "python-kasa>=0.7" tzdata
COPY light.py /light.py
CMD ["python", "/light.py"]
