from flask import Flask, render_template, request
from ultralytics import YOLO
from PIL import Image
import cv2
import numpy as np

app = Flask(__name__)

# 📍 Replace with your actual ESP32-CAM IP address
ESP32_STREAM_URL = "http://192.168.103.125:81/stream"

# 🚀 Load YOLOv8 model
model = YOLO("fi/model/best (3).pt")

@app.route("/") 
def index():
    return render_template("aa.html")


@app.route("/predict", methods=["POST"])
def predict():
    if 'file' not in request.files:
        return "Aucun fichier envoyé"
    
    file = request.files['file']
    image = Image.open(file.stream)

    # 🔍 Predict using YOLOv8
    results = model(image)
    detected_classes = [model.names[int(box.cls)] for box in results[0].boxes]

    if not detected_classes:
        return "Aucune classe détectée"
    return f"Classe(s) prédite(s) : {', '.join(detected_classes)}"


@app.route("/predict_cam")
def predict_cam():
    # 🎥 Open ESP32 stream
    cap = cv2.VideoCapture(ESP32_STREAM_URL)

    if not cap.isOpened():
        return "Impossible d'ouvrir le flux vidéo de l'ESP32-CAM"

    ret, frame = cap.read()
    cap.release()

    if not ret:
        return "Échec de la lecture de l'image"

    # 🖼️ Convert to PIL for YOLO
    image = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))

    # 🔍 Run YOLOv8 prediction
    results = model(image)
    detected_classes = [model.names[int(box.cls)] for box in results[0].boxes]

    if not detected_classes:
        return "Aucune classe détectée"
    return f"Classe(s) prédite(s) : {', '.join(detected_classes)}"


if __name__ == "__main__":
    app.run(debug=True)