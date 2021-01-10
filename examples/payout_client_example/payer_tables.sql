/* this table contains valid blockchain accounts */
CREATE TABLE VALID_ACCOUNTS
(
 account           VARCHAR(13) PRIMARY KEY
) ENGINE=InnoDB;



CREATE TABLE PAYMENTS
(
 payment_uuid      VARCHAR(36) PRIMARY KEY,
 tstamp            DATETIME NOT NULL,
 recipient         VARCHAR(13) NOT NULL,
 amount            DOUBLE PRECISION NOT NULL
) ENGINE=InnoDB;

CREATE INDEX PAYMENTS_I01 ON PAYMENTS (recipient, tstamp);
CREATE INDEX PAYMENTS_I02 ON PAYMENTS (tstamp);


CREATE TABLE PAYMENT_MEMO
(
 recipient         VARCHAR(13) PRIMARY KEY,
 memo              VARCHAR(256) NOT NULL
) ENGINE=InnoDB;




