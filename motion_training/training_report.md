# Synthetic motion classifier

Held-out accuracy: 0.979

## Per-class metrics
```
              precision    recall  f1-score   support

     Walking       0.97      0.96      0.97       250
     Running       0.96      0.97      0.97       250
     Resting       1.00      1.00      1.00       250

    accuracy                           0.98       750
   macro avg       0.98      0.98      0.98       750
weighted avg       0.98      0.98      0.98       750

```

## Learned tree
```
|--- mag_std <= 0.03
|   |--- class: 2
|--- mag_std >  0.03
|   |--- mag_std <= 0.12
|   |   |--- std_z <= 0.21
|   |   |   |--- std_y <= 0.21
|   |   |   |   |--- std_x <= 0.20
|   |   |   |   |   |--- class: 0
|   |   |   |   |--- std_x >  0.20
|   |   |   |   |   |--- class: 0
|   |   |   |--- std_y >  0.21
|   |   |   |   |--- class: 1
|   |   |--- std_z >  0.21
|   |   |   |--- std_x <= 0.16
|   |   |   |   |--- class: 1
|   |   |   |--- std_x >  0.16
|   |   |   |   |--- class: 1
|   |--- mag_std >  0.12
|   |   |--- crossings <= 4.50
|   |   |   |--- mag_std <= 0.22
|   |   |   |   |--- class: 0
|   |   |   |--- mag_std >  0.22
|   |   |   |   |--- class: 1
|   |   |--- crossings >  4.50
|   |   |   |--- std_y <= 0.09
|   |   |   |   |--- class: 0
|   |   |   |--- std_y >  0.09
|   |   |   |   |--- std_z <= 0.09
|   |   |   |   |   |--- class: 0
|   |   |   |   |--- std_z >  0.09
|   |   |   |   |   |--- class: 1

```

Model: 21 nodes, max depth 5.
Data are simulated; held-out scores measure synthetic consistency, not real-world performance.