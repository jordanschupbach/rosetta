      *> The generated copybooks also compile in fixed-format programs.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. ROSETTA-COBOL-FIXED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       COPY "cobol_demo.cpy".
       COPY "cobol_verify.cpy".
       COPY "cobol_names.cpy".
       PROCEDURE DIVISION.
           MOVE 20 TO COBOL-DEMO-ADD-A
           MOVE 22 TO COBOL-DEMO-ADD-B
           CALL "cobol_demo_add" USING COBOL-DEMO-ADD COBOL-DEMO-STATUS
           IF COBOL-DEMO-STATUS-OK AND COBOL-DEMO-ADD-RESULT = 42
               DISPLAY "ROSETTA PASS fixed"
           ELSE
               DISPLAY "FAIL fixed-format call"
           END-IF
           STOP RUN.
