abracadabra {
    iGlobal;
    num contadorLinea = 0;
    num yagir = 0;
    fGlobal;

    bool sensorDerecho;
    bool sensorIzquierdo;

    if (contadorLinea == 0){
        avanzar;
    }

    if (sensorDerecho AND sensorIzquierdo) {
        contadorLinea = contadorLinea + 1;
        delay;
    }

    if (yagir == 1) {
        yagir = 2;
        avanzar;
    }

    if (contadorLinea == 5 AND yagir == 0) {
        girarDer;
        yagir = 1;
        delay;
    }
}