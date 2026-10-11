# Motor Control and PID Development Notes and Documentation

The purpose of this document is to explain code, development decisions and current implementation

### Current Implementation

A system-level view of the code is as follows:

```
Stabilization Task
│
├── PID_stabilization.c
|   |
|   ├── pid_init()
|   |   └── Initialize state/configuration
|   |
|   ├── pid_tune()
|   |   └── Set tunable parameters (Kp, Ki, Kd, Filter time constant)
│   │
│   ├── pid_calculate()
│   |   ├── Calculates single-axis control correction from:
│   |   |   ├── Target platform angle
│   |   |   ├── Measured IMU angle
|   |   └── Performs output command clean-up:
|   |       ├── Derivative gain filtering
|   |       └── Anti-windup and motor saturation consideration
│   └── pid_reset()
|       └── clears runtime history (time dependent PID components)
|
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

-

### Pending Decisions

- task timing implementatio (higher level - RTOS or Seq)
- MCPWM vs LEDC -- is LEDC sufficent as a PWM controller?