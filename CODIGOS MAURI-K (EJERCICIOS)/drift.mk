abracadabra {
    iGlobal;
    num yagir = 0;
    fGlobal;

    bool sensorDerecho;
    bool sensorIzquierdo;

    if (yagir == 3) {
        avanzar;
        delay;
        yagir = 0;
    }

    if (yagir == 2) {
        avanzar;
        delay;
        yagir = 3;
    }

    if (yagir == 1){
        girarDer;
        delay;
        yagir = 2;
    }

    if (yagir == 0){
        girarDer;
        delay;
        yagir = 1;
    }

}