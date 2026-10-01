import cv2
from ultralytics import YOLO

# Charger le modèle
model = YOLO("fi/model/best (3).pt")
print("Classes du modèle :", model.names)

# Ouvrir le flux vidéo
video_path = 0
cap = cv2.VideoCapture(video_path)

while cap.isOpened():
    success, frame = cap.read()

    if success:
        # Prédiction
        results = model(frame)
        r = results[0]
        boxes = r.boxes

        if boxes is not None and boxes.cls is not None and len(boxes.cls) > 0:
            detected_classes = [model.names[int(cls)] for cls in boxes.cls]
            print("Classes détectées :", detected_classes)
        else:
            print("Aucune classe détectée")

        # Affichage
        annotated_frame = r.plot()
        cv2.imshow("YOLO Inference", annotated_frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break
    else:
        print("Erreur : Impossible de lire une frame du flux")
        break

cap.release()
cv2.destroyAllWindows()
