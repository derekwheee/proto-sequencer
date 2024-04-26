#include <map>

struct NoteMap
{
    double voltage;
    char note;
    bool isSharp;
    int octave;
};

NoteMap VoltToNoteMap[84];

NoteMap getNoteFromVoltage(float voltage)
{
    NoteMap lastNoteMap;

    for (NoteMap noteMap : VoltToNoteMap) {
        if (noteMap.voltage <= voltage) {
            lastNoteMap = noteMap;
        }
    }

    return lastNoteMap;
}

void fillNoteMap()
{
    for (int i = 0; i < 7; i++)
    {
        VoltToNoteMap[i * 12 + 0] = {0.0 + i, 'C', false, i};
        VoltToNoteMap[i * 12 + 1] = {0.08 + i, 'C', true, i};
        VoltToNoteMap[i * 12 + 2] = {0.16 + i, 'D', false, i};
        VoltToNoteMap[i * 12 + 3] = {0.25 + i, 'D', true, i};
        VoltToNoteMap[i * 12 + 4] = {0.33 + i, 'E', false, i};
        VoltToNoteMap[i * 12 + 5] = {0.41 + i, 'F', false, i};
        VoltToNoteMap[i * 12 + 6] = {0.50 + i, 'F', true, i};
        VoltToNoteMap[i * 12 + 7] = {0.58 + i, 'G', false, i};
        VoltToNoteMap[i * 12 + 8] = {0.66 + i, 'G', true, i};
        VoltToNoteMap[i * 12 + 9] = {0.75 + i, 'A', false, i};
        VoltToNoteMap[i * 12 + 10] = {0.83 + i, 'A', true, i};
        VoltToNoteMap[i * 12 + 11] = {0.91 + i, 'B', false, i};
    }
}