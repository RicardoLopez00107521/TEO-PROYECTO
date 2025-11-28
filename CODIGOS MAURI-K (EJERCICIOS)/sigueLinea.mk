abracadabra {  
    bool sensorDerecho;
    bool sensorIzquierdo;

    if(sensorDerecho AND sensorIzquierdo) {
        avanzar;
    }

    if(sensorDerecho AND NOT sensorIzquierdo) {
        girarDer;
    }

    if(NOT sensorDerecho AND sensorIzquierdo) {
        girarIzq;
    }
}