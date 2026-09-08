# Motor Control and PID Development Notes and Documentation

The purpose of this document is to explain code, development decisions and current implementation

### Current Implementation

A system-level view of the code is as follows:

```
Stabilization Task
│
├── PID_stabilization.c
│   │
│   └── pid_calculate()
│       └── Calculates single-axis control correction from:
│           ├── Target platform angle
│           └── Measured IMU angle
│
└── motor_control.c
    │
    ├── motor_init()
    │   └── Initializes roll/pitch servos using LEDC
    │
    ├── command_motor_angle()
    │   └── Abstracts servo angle commands
    │
    └── motor_del()
        └── Deletes servo instances
```

### Under Development

- PID_stabilization.c is rudimentary, does not include real-world constraints such as command limits (min-max), actuator saturation, derivative sensitivity to noise (needs filter), actual timing input and rate limiting for command delta 
- PID and motor functions do not currently interact, stabilization.c needs work
- PID parameters Kp, Ki, Kd are untuned

### Pending Decisions

- task frequency
- MCPWM vs LEDC -- is LEDC sufficent as a PWM controller?