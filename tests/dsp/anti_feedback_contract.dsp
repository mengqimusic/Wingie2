import("stdfaust.lib");

blockSize = 32;
blockPulse = ba.time % blockSize == 0;
blockHold(x) = x : control(blockPulse) : ba.sAndH(blockPulse);

frequency = hslider("frequency", 440, 16, 16000, 0.01);
t60 = hslider("t60", 10, 0.1, 10, 0.01);
energyLimit = hslider("energy_limit", 0.25, 0.000001, 64, 0.000001);
rhoGuard = hslider("rho_guard", 0.5, 0.001, 0.999999, 0.000001);
enabled = hslider("enabled", 1, 0, 1, 1);

controlledRotationStep(sine, cosine, rhoUser, guard, limit, active, x,
                       qPrevious, pPrevious, peakPrevious,
                       gainPrevious, rhoPrevious) = q, p, peak, gain, rho
with {
    overload = max(1, peakPrevious / limit)
        : control(blockPulse) : ba.sAndH(blockPulse);
    guardedRho = min(rhoUser, guard)
        : control(blockPulse) : ba.sAndH(blockPulse);
    boundaryGain = select2(active > 0, 1, 1 / overload);
    boundaryRho = select2((active > 0) & (overload > 1),
                          rhoUser, guardedRho);
    gain = select2(blockPulse, gainPrevious, boundaryGain);
    rho = select2(blockPulse, rhoPrevious, boundaryRho);
    q = rho * (sine * pPrevious + cosine * qPrevious);
    p = gain * x + rho * (cosine * pPrevious - sine * qPrevious);
    energy = q * q + p * p;
    peak = select2(blockPulse, max(energy, peakPrevious), energy);
};

controlledRotation(sine, cosine, rhoUser, guard, limit, active, x) =
    controlledRotationStep(sine, cosine, rhoUser, guard, limit, active, x)
    ~ si.bus(5);

rhoUser = pow(0.001, 1.0 / (t60 * ma.SR)) : blockHold;
theta = 2 * ma.PI * frequency / ma.SR;
sine = sin(theta) : blockHold;
cosine = cos(theta) : blockHold;

process = _ : controlledRotation(sine, cosine, rhoUser,
                                 rhoGuard, energyLimit, enabled);
