#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Relais (actif LOW) ──
#define EV1 2   // sortie réservoir
#define EV2 3   // entrée chambre stérilisation
#define EV3 4   // pompe vers stockage

// ── LEDs UV ──
#define UV_PIN 5

// ── Capteurs ultrasons ──
#define TRIG1 8   // US1 → réservoir source
#define ECHO1 9
#define TRIG2 10  // US2 → chambre stérilisation
#define ECHO2 11

// ── Seuils (distances en cm — plus la distance est grande, plus le niveau est bas) ──
#define RES_VIDE       11.5    // distance > 25 cm → réservoir trop bas → EV1 OFF
#define STERIL_MIN      2
   // distance < 10 cm → chambre assez pleine → démarrer UV
#define STERIL_VIDE     4   // distance > 22 cm → chambre vidée → fin pompage

// ── Durée de stérilisation UV ──
#define DUREE_UV_MS     100000   // 30 secondes (à ajuster)

// ── Machine à états ──
enum Etat { REMPLISSAGE, FILTRATION_ATTENTE, STERILISATION, POMPAGE_STOCKAGE };
Etat etatCourant = REMPLISSAGE;
unsigned long debutUV = 0;

// ── Helpers relais ──
void ouvrirEV(int pin)  { digitalWrite(pin, LOW); }
void fermerEV(int pin)  { digitalWrite(pin, HIGH); }

// ── Mesure ultrason ──
long distanceCm(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long d = pulseIn(echo, HIGH, 30000);
  return (d == 0) ? 999 : d * 0.034 / 2;
}

// ── Affichage LCD (2 lignes) ──
void afficher(const char* l1, const char* l2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

void uvON()  { digitalWrite(UV_PIN, HIGH);}
void uvOFF() { digitalWrite(UV_PIN, LOW); }

void setup() {
  Serial.begin(9600);
  pinMode(EV1, OUTPUT); fermerEV(EV1);
  pinMode(EV2, OUTPUT); fermerEV(EV2);
  pinMode(EV3, OUTPUT); fermerEV(EV3);
  pinMode(UV_PIN, OUTPUT); uvOFF();
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);

  lcd.init();
  lcd.backlight();
  afficher("Systeme eau", "Demarrage...");
  delay(2000);
}

void loop() {
  long d1 = distanceCm(TRIG1, ECHO1);  // réservoir source
  long d2 = distanceCm(TRIG2, ECHO2);  // chambre stérilisation
  delay(60);  // pause entre les deux mesures ultrasons
  Serial.print("Distance US1 :" );
  Serial.print(d1);
  Serial.print("cm \n");
  Serial.print("Ditance US2 :");
  Serial.print(d2);
  Serial.print("cm\n");

  switch (etatCourant) {

    // ─────────────────────────────────────────────
    case REMPLISSAGE:
      // Réservoir a assez d'eau → EV1 ouverte
      if (d1 < RES_VIDE) {
        ouvrirEV(EV1);
        ouvrirEV(EV2);   // laisse entrer dans la chambre de stérilisation
        fermerEV(EV3);
        uvOFF();
        char buf[16];
        snprintf(buf, 16, "Res:%ldcm Fil:%ldcm", d1, d2);
        afficher("1-Remplissage UV", buf);
        // Si la chambre de stérilisation est assez pleine → passer en UV
        if (d2 < STERIL_MIN) {
          fermerEV(EV1);
          delay(30000);
          fermerEV(EV2);  // IMPORTANT : fermer avant d'allumer UV
          etatCourant = STERILISATION;
        }
      } else {
        // Réservoir trop bas
        fermerEV(EV1);
        fermerEV(EV2);
        afficher("! Reservoir bas", "EV1 fermee");
        if (d2 < STERIL_VIDE){ 
          etatCourant = STERILISATION;
        }

      }
      break;

    // ─────────────────────────────────────────────
    case STERILISATION:
      fermerEV(EV1);
      fermerEV(EV2);  // fermée pendant toute la phase UV
      fermerEV(EV3);
      uvON();
      if (debutUV == 0) debutUV = millis();

      {
        unsigned long elapsed = (millis() - debutUV) / 1000;
        char buf[16];
        snprintf(buf, 16, "UV ON  %lus/%lus", elapsed, DUREE_UV_MS/1000);
        afficher("3-Sterilisation ", buf);
      }

      // Fin du cycle UV
      if (millis() - debutUV >= DUREE_UV_MS) {
        uvOFF();
        debutUV = 0;
        etatCourant = POMPAGE_STOCKAGE;
      }
      break;

    // ─────────────────────────────────────────────
    case POMPAGE_STOCKAGE:
      fermerEV(EV1);
      fermerEV(EV2);
      uvOFF();
      ouvrirEV(EV3);  // pompe vers stockage
      afficher("4-Pompage stock.", "EV3 ouverte...");

      // Chambre vidée → retour au remplissage
      if (d1 >= RES_VIDE && d2 < STERIL_VIDE){ 
        fermerEV(EV1);
        fermerEV(EV2);
        uvOFF();
        ouvrirEV(EV3);
        afficher("5-Pompage de l'eau restante","EV3 ouverte...");
      }
      if (d2 > STERIL_VIDE) {
        fermerEV(EV3);
        etatCourant = REMPLISSAGE;
        afficher("Stockage OK", "Nouveau cycle...");
        delay(2000);
      }
      break;
  }

  delay(400);
}