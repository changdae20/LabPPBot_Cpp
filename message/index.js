const express = require("express");
const moment = require("moment");
const fs = require("fs");
const app = express();
const port = 13931;

app.use(express.json()); 
app.use(express.urlencoded( {extended : false } ));

app.post("/message", (req, res) => {
    console.log(req.body);
    fs.writeFile(`./data/${moment().format("YYYYMMDDHHmmss")}.txt`, JSON.stringify(req.body), (err) => {
        if(err){
            console.log(err);
            res.send("ERROR");
        }
    });
    res.send("OK");
});

app.listen(port, "0.0.0.0", () => {
    console.log(`Message Listening at : http://0.0.0.0:${port}`);
});